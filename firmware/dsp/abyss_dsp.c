/* ABYSS sound engine. Portable C, 32-bit fixed point only (no FPU on the AC79; no 64-bit multiplies in the
 * per-sample loops: docs/ARCHITECTURE.md, "CPU"). Built into the firmware's translation unit and into the host
 * test (host/render.c): every symbol is static.
 *
 * Signal path (Milestone 2):
 *   OSC 1 ┐
 *   OSC 2 ┼─ mixer (levels, MIX balance) ─ SVF low-pass (2x oversampled, saturating) ─ VCA (ADSR or DRONE)
 *   OSC 3 ┤                                                                              ─ soft clip ─ MASTER ─ L/R
 *   SUB   ┤
 *   NOISE ┘
 * Samples are Q15 (32768 = full scale) in int32. Control values are recomputed once per block of AB_BLOCK. */
#include "abyss_params.h"

#define AB_BLOCK 32u
#define AB_SCOPE_N 512u

typedef struct {
    int16_t p[P_COUNT];          /* written by the UI */
    volatile uint8_t note;       /* MIDI note of the voice */
    volatile uint8_t gate;       /* a key is held */
    volatile uint8_t drone;      /* PLAY: the voice sounds without a key */
    volatile uint8_t trig;       /* counts note starts: a change retriggers the envelope */
    volatile int16_t master;     /* Q15 output level (the MASTER pot) */
} ab_ctl_t;

static ab_ctl_t ab;

static struct {
    uint32_t ph[3], sub_ph;      /* oscillator phases (free running: an analog drone never restarts) */
    uint32_t rng;
    int32_t pk0, pk1, pk2;       /* pink noise poles */
    int32_t lp, bp;              /* SVF state */
    int32_t f_cur;               /* SVF coefficient, Q14, smoothed */
    int32_t g[5];                /* mixer gains, Q15, smoothed: osc 1..3, sub, noise */
    int32_t env;                 /* Q24 */
    uint8_t stage, last_trig;    /* 0 idle, 1 attack, 2 decay / sustain, 3 release */
    int32_t vca;                 /* Q15 level at the end of the last block */
    int32_t glide_pitch;         /* 1/64 semitone */
} st;

static int16_t ab_scope[AB_SCOPE_N];   /* the output, every other sample: the UI's waveform */
static volatile uint32_t ab_scope_w;
static volatile int32_t ab_peak;       /* the largest output since the UI last looked (Q15) */

/* ---------------------------------------------------------------- helpers --- */
static inline int32_t ab_clamp(int32_t x, int32_t lo, int32_t hi) { return x < lo ? lo : x > hi ? hi : x; }

static uint32_t ab_pitch_inc(int32_t p)          /* 1/64 semitone from MIDI 0 -> phase increment */
{
    int32_t sh = 0;
    p = ab_clamp(p, 0, AB_PITCH_TOP + 767);
    while (p < AB_PITCH_TOP) {
        p += 768;
        sh++;
    }
    return AB_OCT_INC[p - AB_PITCH_TOP] >> sh;
}

/* PolyBLEP residual at a discontinuity at phase 0, Q15 (amplitude 1 = 32768) */
static inline int32_t ab_blep(uint32_t ph, uint32_t inc, uint32_t div)
{
    int32_t t;
    if (ph < inc) {
        t = (int32_t)(ph / div);
        if (t > 32767) t = 32767;
        return 2 * t - ((t * t) >> 15) - 32768;
    }
    if (ph > 0u - inc) {
        t = (int32_t)((0u - ph) / div);
        if (t > 32767) t = 32767;
        return ((t * t) >> 15) - 2 * t + 32768;
    }
    return 0;
}

static inline int32_t ab_osc(uint32_t w, uint32_t ph, uint32_t inc, uint32_t div, uint32_t pw)
{
    switch (w) {
    case W_SINE: {
        uint32_t i = ph >> 22, fr = (ph >> 6) & 0xFFFFu;
        int32_t a = AB_SINE[i], b = AB_SINE[i + 1u];
        return a + (((b - a) * (int32_t)fr) >> 16);
    }
    case W_TRI:
        return ph < 0x80000000u ? (int32_t)(ph >> 15) - 32768 : 32767 - (int32_t)((ph - 0x80000000u) >> 15);
    case W_SAW:
        return (int32_t)(ph >> 16) - 32768 - ab_blep(ph, inc, div);
    default:                                     /* square / pulse */
        return (ph < pw ? 32767 : -32767) + ab_blep(ph, inc, div) - ab_blep(ph - pw, inc, div);
    }
}

static inline uint32_t ab_rand(void)
{
    uint32_t x = st.rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return st.rng = x;
}

/* cubic soft clip: linear-ish to 0.5, 1.0 at 1.5 and above. Q15 in, Q15 out */
static inline int32_t ab_softclip(int32_t x)
{
    int32_t q = ab_clamp(x, -49152, 49152) >> 1;  /* Q14, |q| <= 1.5 */
    int32_t x2 = (q * q) >> 14;
    int32_t x3 = (x2 * q) >> 14;
    return (q - ((x3 * 4855) >> 15)) << 1;
}

/* ---------------------------------------------------------------- envelope --- */
static void ab_env_block(void)
{
    uint32_t gate = ab.gate || ab.drone;
    if (ab.trig != st.last_trig) {               /* a new note: from where it is (no click) */
        st.last_trig = ab.trig;
        st.stage = 1;
    } else if (gate && (st.stage == 0u || st.stage == 3u)) {   /* (a key, or DRONE on, while releasing) */
        st.stage = 1;
    }
    if (!gate && st.stage && st.stage < 3u)
        st.stage = 3;
    switch (st.stage) {
    case 1:
        st.env += (int32_t)AB_ENV_STEP[ab.p[P_ATK]];
        if (st.env >= 1 << 24) {
            st.env = 1 << 24;
            st.stage = 2;
        }
        break;
    case 2: {
        int32_t sus = ab.drone ? 1 << 24 : (ab.p[P_SUS] << 24) / 100;
        st.env -= (((st.env - sus) >> 10) * (int32_t)AB_ENV_K[ab.p[P_DEC]]) >> 6;
        break;
    }
    case 3:
        st.env -= ((st.env >> 10) * (int32_t)AB_ENV_K[ab.p[P_REL]]) >> 6;
        if (st.env < 256) {
            st.env = 0;
            st.stage = 0;
        }
        break;
    }
}

/* ---------------------------------------------------------------- engine --- */
static void dsp_init(void)
{
    uint32_t i;
    uint8_t *z = (uint8_t *)&st;
    for (i = 0; i < sizeof st; i++)
        z[i] = 0;
    ab.gate = ab.drone = 0;
    st.last_trig = ab.trig;
    for (i = 0; i < P_COUNT; i++)
        ab.p[i] = AB_P[i].def;
    ab.note = 69;
    ab.master = 26000;
    st.rng = 0x2545F491u;
    st.ph[1] = 0x55555555u;                      /* the oscillators do not start in phase */
    st.ph[2] = 0xAAAAAAAAu;
    st.f_cur = AB_CUT_F[AB_P[P_CUTOFF].def];
}

/* the frequency OSC 1 plays now, in 0.1 Hz (the screen's FREQUENCY) */
static __attribute__((noinline)) uint32_t dsp_osc1_hz10(void)
{
    uint32_t inc = ab_pitch_inc((int32_t)ab.note * 64 + ab.p[P_OSC1] * 64);
    return (inc + 4868u) / 9735u;                /* 2^32 / (FS x 10): no 64-bit multiply */
}

static int32_t pct_q15(int32_t v) { return v * 32767 / 100; }

static void dsp_render(int32_t *out, uint32_t n)        /* n == AB_BLOCK; out: L, R interleaved, Q15 */
{
    uint32_t i, inc[3], div[3], sinc, sdiv, pw, w[3], nt;
    int32_t base, g_t[5], mixa, mixb, f_t, damp, cut, e0, e1, master;

    /* ---- control rate ---- */
    base = (int32_t)ab.note * 64;
    {
        int32_t det = ab.p[P_DETUNE] * 64 / 100;    /* cents -> 1/64 semitone */
        inc[0] = ab_pitch_inc(base + ab.p[P_OSC1] * 64);
        inc[1] = ab_pitch_inc(base + ab.p[P_OSC1] * 64 + ab.p[P_OSC2] * 64 + det);
        inc[2] = ab_pitch_inc(base + ab.p[P_OSC1] * 64 + ab.p[P_OSC3] * 64 - det);
    }
    sinc = inc[0] >> 1;
    for (i = 0; i < 3u; i++) {
        div[i] = (inc[i] >> 15) | 1u;
        w[i] = (uint32_t)ab.p[P_WAVE1 + i];
    }
    sdiv = (sinc >> 15) | 1u;
    pw = (uint32_t)ab.p[P_PW] * 42949673u;       /* % -> phase */
    nt = (uint32_t)ab.p[P_NTYPE];

    mixa = ab.p[P_MIX] <= 50 ? 32767 : (100 - ab.p[P_MIX]) * 32767 / 50;   /* OSC 1 side */
    mixb = ab.p[P_MIX] >= 50 ? 32767 : ab.p[P_MIX] * 32767 / 50;           /* OSC 2 + 3 side */
    g_t[0] = (pct_q15(ab.p[P_LVL1]) * mixa) >> 15;
    g_t[1] = (pct_q15(ab.p[P_LVL2]) * mixb) >> 15;
    g_t[2] = (pct_q15(ab.p[P_LVL3]) * mixb) >> 15;
    g_t[3] = (pct_q15(ab.p[P_SUB]) * mixa) >> 15;
    g_t[4] = pct_q15(ab.p[P_NOISE]);
    for (i = 0; i < 5u; i++)
        st.g[i] += (g_t[i] - st.g[i]) >> 2;

    ab_env_block();
    cut = ab.p[P_CUTOFF] * 256 + ((ab.p[P_ENVF] * (st.env >> 9)) >> 15) * 325;   /* +-100 % = +-127 knob steps */
    cut = ab_clamp(cut, 0, 127 * 256);
    {
        int32_t k = cut >> 8, fr = cut & 255;
        f_t = AB_CUT_F[k] + (((AB_CUT_F[k + 1] - AB_CUT_F[k]) * fr) >> 8);
    }
    st.f_cur += (f_t - st.f_cur) >> 1;
    damp = 32767 - ab.p[P_RES] * 32000 / 100;    /* Q14: 2.0 (no resonance) .. ~0.05 (rings, self-oscillates) */

    e0 = st.vca;
    e1 = st.env >> 9;                            /* Q15 */
    st.vca = e1;
    master = ab.master;

    /* ---- audio rate ---- */
    for (i = 0; i < n; i++) {
        int32_t s, x, hp, nz, e, k;
        uint32_t r = ab_rand();
        s = (ab_osc(w[0], st.ph[0], inc[0], div[0], pw) * st.g[0]) >> 15;
        s += (ab_osc(w[1], st.ph[1], inc[1], div[1], pw) * st.g[1]) >> 15;
        s += (ab_osc(w[2], st.ph[2], inc[2], div[2], pw) * st.g[2]) >> 15;
        s += (ab_osc(W_SQR, st.sub_ph, sinc, sdiv, 0x80000000u) * st.g[3]) >> 15;
        st.ph[0] += inc[0];
        st.ph[1] += inc[1];
        st.ph[2] += inc[2];
        st.sub_ph += sinc;

        nz = (int32_t)r >> 16;                   /* white, Q15 */
        if (nt == N_PINK) {                      /* Kellet's economy pink: three poles, -3 dB / octave */
            st.pk0 = ((st.pk0 * 32691) >> 15) + ((nz * 3245) >> 15);
            st.pk1 = ((st.pk1 * 31556) >> 15) + ((nz * 9716) >> 15);
            st.pk2 = ((st.pk2 * 18678) >> 15) + ((nz * 34494) >> 15);
            nz = (st.pk0 + st.pk1 + st.pk2 + ((nz * 6056) >> 15)) >> 2;
        }
        s += (nz * st.g[4]) >> 15;

        /* SVF low-pass, run twice (2x oversampled for stability at high cutoff); the band-pass state saturates,
         * so high resonance growls instead of exploding */
        x = ab_clamp(s >> 1, -65535, 65535);
        for (k = 0; k < 2; k++) {
            st.lp += (st.f_cur * st.bp) >> 14;
            hp = ab_clamp(x - st.lp - ((damp * st.bp) >> 14), -131071, 131071);
            st.bp += (st.f_cur * hp) >> 14;
            st.bp = ab_clamp(st.bp, -65535, 65535);
            st.lp = ab_clamp(st.lp, -65535, 65535);
        }
        x = st.lp;

        e = e0 + (((e1 - e0) * (int32_t)i) >> 5);  /* the envelope ramped over the block */
        x = (x * e) >> 15;
        x = ab_softclip(x);
        x = (x * master) >> 15;
        out[2u * i] = x;
        out[2u * i + 1u] = x;
        if (i & 1u)
            ab_scope[ab_scope_w++ & (AB_SCOPE_N - 1u)] = (int16_t)x;
        if (x < 0)
            x = -x;
        if (x > ab_peak)
            ab_peak = x;
    }
}
