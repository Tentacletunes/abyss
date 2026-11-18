/* ABYSS sound engine. Portable C, 32-bit fixed point only (no FPU on the AC79; no 64-bit multiplies in the
 * per-sample loops: docs/ARCHITECTURE.md, "CPU"). Built into the firmware's translation unit and into the host
 * test (host/render.c): every symbol is static.
 *
 * Signal path:
 *            ┌── FM (2 > 1), CROSS MOD (1 <> 3), CHAOS (drift + noise FM), SYNC (2, 3 to 1) ──┐
 *   OSC 1 ┐  │                                                                                │
 *   OSC 2 ┼──┴─ mixer (levels, MIX balance, RING 1 x 2, SUB, NOISE) ─ SVF low-pass (2x oversampled, saturating)
 *   OSC 3 ┤                                                           ─ VCA (ADSR or DRONE, LEVEL mod) ─ soft clip
 *   SUB   ┤                                                           ─ MASTER ─ L/R
 *   NOISE ┘  white, pink, brown, digital (LFSR), metal (short LFSR), crackle
 * Modulation, once per block: LFO 1, LFO 2 (sine tri square saw random), RANDOM (sample and hold, smoothed), each
 * with a depth and one destination: pitch, OSC 2 pitch, filter, resonance, level, FM, noise.
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
    int32_t lvl;                 /* LEVEL modulation at the end of the last block, Q15 */
    int32_t o[3];                /* the oscillators' last outputs (FM, cross mod) */
    int32_t drift[3];            /* CHAOS: each oscillator's slow random wander */
    uint32_t lfo_ph[2];
    int32_t lfo_hold[2];         /* LFO RANDOM: the value of this cycle */
    uint32_t rnd_ph;
    int32_t rnd_tgt, rnd_cur;    /* RANDOM: the sampled value, and where the smoothing is */
    int32_t brown;               /* brown noise integrator */
    uint32_t nph, lfsr;          /* DIGITAL / METAL: the register's clock and the register */
    int32_t crk;                 /* CRACKLE: the decaying impulse */
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
    return (q - ((x3 * 4855) >> 15)) * 2;
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
    st.lfsr = 1u;
    st.lvl = 32767;
}

/* the frequency OSC 1 plays now, in 0.1 Hz (the screen's FREQUENCY) */
static __attribute__((noinline)) uint32_t dsp_osc1_hz10(void)
{
    uint32_t inc = ab_pitch_inc((int32_t)ab.note * 64 + ab.p[P_OSC1] * 64);
    return (inc + 4868u) / 9735u;                /* 2^32 / (FS x 10): no 64-bit multiply */
}

static int32_t pct_q15(int32_t v) { return v * 32767 / 100; }

/* an LFO's value this block, Q15 bipolar; the phase moves one block */
static int32_t ab_lfo(uint32_t k, uint32_t wave, uint32_t rate)
{
    uint32_t ph = st.lfo_ph[k], nph = ph + AB_LFO_INC[rate];
    int32_t v;
    if (nph < ph)                                /* a new cycle: RANDOM takes a new value */
        st.lfo_hold[k] = (int32_t)ab_rand() >> 16;
    st.lfo_ph[k] = nph;
    switch (wave) {
    case LW_SINE: v = AB_SINE[ph >> 22]; break;
    case LW_TRI: v = ph < 0x80000000u ? (int32_t)(ph >> 15) - 32768 : 32767 - (int32_t)((ph - 0x80000000u) >> 15); break;
    case LW_SQR: v = ph < 0x80000000u ? 32767 : -32767; break;
    case LW_SAW: v = 32767 - (int32_t)(ph >> 16); break;   /* falling: a pluck-like sweep */
    default: v = st.lfo_hold[k]; break;
    }
    return v;
}

/* phase increment inc bent by m (Q15, 32768 = index 1, |m| <= 65535): linear FM, through zero (a negative result
 * runs the phase backwards). 32-bit products only: inc above ~11 kHz is not bent further */
static inline int32_t ab_fm(uint32_t inc, int32_t m)
{
    int32_t b = (int32_t)(inc > 0x3FFFFFFFu ? 0x3FFFFFFFu : inc), a = b >> 18, d;   /* (<= 11 kHz) */
    if (a > 4095) a = 4095;
    m = ab_clamp(m, -65535, 65535);
    d = (a * m) * 8;
    return d > 0x7FFFFFFF - b ? 0x7FFFFFFF : b + d;
}

static inline uint32_t ab_abs_inc(int32_t inc) { return inc < 0 ? (uint32_t)-inc : (uint32_t)inc; }

static void dsp_render(int32_t *out, uint32_t n)        /* n == AB_BLOCK; out: L, R interleaved, Q15 */
{
    uint32_t i, k, inc[3], sinc, sdiv, pw, w[3], nt, sync, ratio[3], ninc;
    int32_t base, g_t[5], mixa, mixb, f_t, damp, cut, e0, e1, l0, l1, master, mod[D_COUNT], dsum_lvl;
    int32_t fm, xm, ring, chaos, res, noise;

    /* ---- modulation sources ---- */
    for (k = 0; k < D_COUNT; k++)
        mod[k] = 0;
    dsum_lvl = 0;
    {
        static const uint8_t RATE[3] = {P_L1RATE, P_L2RATE, P_RRATE}, DEPTH[3] = {P_L1DEPTH, P_L2DEPTH, P_RDEPTH},
                             DEST[3] = {P_L1DEST, P_L2DEST, P_RDEST};
        int32_t v[3];
        v[0] = ab_lfo(0, (uint32_t)ab.p[P_L1WAVE], (uint32_t)ab.p[P_L1RATE]);
        v[1] = ab_lfo(1, (uint32_t)ab.p[P_L2WAVE], (uint32_t)ab.p[P_L2RATE]);
        {                                        /* RANDOM: sample and hold, then a one-pole SMOOTH */
            uint32_t ph = st.rnd_ph;
            st.rnd_ph += AB_LFO_INC[ab.p[P_RRATE]];
            if (st.rnd_ph < ph)
                st.rnd_tgt = (int32_t)ab_rand() >> 16;
            if (!ab.p[P_RSMOOTH])
                st.rnd_cur = st.rnd_tgt;
            else {                               /* 1 .. 100 %: from a quick slew to most of a step */
                int32_t sh = 1 + ab.p[P_RSMOOTH] / 12;
                st.rnd_cur += (st.rnd_tgt - st.rnd_cur) >> sh;
            }
            v[2] = st.rnd_cur;
        }
        for (k = 0; k < 3u; k++) {
            int32_t d = pct_q15(ab.p[DEPTH[k]]), dst = ab.p[DEST[k]];
            (void)RATE;
            mod[dst] += (v[k] * d) >> 15;
            if (dst == D_LEVEL)
                dsum_lvl += d;
        }
    }

    /* ---- pitch ---- */
    chaos = ab.p[P_CHAOS];
    for (k = 0; k < 3u; k++) {                   /* CHAOS: three slow, separate random walks */
        int32_t r = (int32_t)ab_rand() >> 16;
        st.drift[k] += (r - st.drift[k]) >> 8;
    }
    base = (int32_t)ab.note * 64 + ab.p[P_OSC1] * 64 + ((mod[D_PITCH] * 12 * 64) >> 15);
    {
        int32_t det = ab.p[P_DETUNE] * 64 / 100;    /* cents -> 1/64 semitone */
        int32_t o2 = (mod[D_OSC2] * 12 * 64) >> 15;
        int32_t d0 = ab_clamp((st.drift[0] * chaos) >> 9, -768, 768);   /* +-12 semitones at most */
        int32_t d1 = ab_clamp((st.drift[1] * chaos) >> 9, -768, 768);
        int32_t d2 = ab_clamp((st.drift[2] * chaos) >> 9, -768, 768);
        inc[0] = ab_pitch_inc(base + d0);
        inc[1] = ab_pitch_inc(base + ab.p[P_OSC2] * 64 + det + o2 + d1);
        inc[2] = ab_pitch_inc(base + ab.p[P_OSC3] * 64 - det + d2);
    }
    sinc = inc[0] >> 1;
    for (k = 0; k < 3u; k++) {
        w[k] = (uint32_t)ab.p[P_WAVE1 + k];
        ratio[k] = inc[k] / ((inc[0] >> 16) | 1u);   /* slave / master, Q16: where a synced slave restarts */
    }
    sdiv = (sinc >> 15) | 1u;
    pw = (uint32_t)ab.p[P_PW] * 42949673u;       /* % -> phase */
    nt = (uint32_t)ab.p[P_NTYPE];
    sync = (uint32_t)ab.p[P_SYNC];
    ninc = inc[0] > 0x0FFFFFFFu ? 0xFFFFFFFFu : inc[0] << 4;   /* DIGITAL / METAL clock: 16 x OSC 1 */
    if (nt == N_METAL)
        ninc >>= 1;

    fm = ab_clamp(ab.p[P_FM] + ((mod[D_FM] * 100) >> 15), 0, 100) * 655;   /* 100 % = index 2 */
    xm = ab.p[P_XMOD] * 491;                     /* 100 % = index 1.5 */
    ring = pct_q15(ab.p[P_RING]);
    chaos = chaos * 82;                          /* noise FM: 100 % = index 0.25 */

    /* ---- mixer ---- */
    mixa = ab.p[P_MIX] <= 50 ? 32767 : (100 - ab.p[P_MIX]) * 32767 / 50;   /* OSC 1 side */
    mixb = ab.p[P_MIX] >= 50 ? 32767 : ab.p[P_MIX] * 32767 / 50;           /* OSC 2 + 3 side */
    noise = ab_clamp(ab.p[P_NOISE] + ((mod[D_NOISE] * 100) >> 15), 0, 100);
    g_t[0] = (pct_q15(ab.p[P_LVL1]) * mixa) >> 15;
    g_t[1] = (pct_q15(ab.p[P_LVL2]) * mixb) >> 15;
    g_t[2] = (pct_q15(ab.p[P_LVL3]) * mixb) >> 15;
    g_t[3] = (pct_q15(ab.p[P_SUB]) * mixa) >> 15;
    g_t[4] = pct_q15(noise);
    for (k = 0; k < 5u; k++)
        st.g[k] += (g_t[k] - st.g[k]) >> 2;

    /* ---- filter, envelope ---- */
    ab_env_block();
    cut = ab.p[P_CUTOFF] * 256 + ((ab.p[P_ENVF] * (st.env >> 9)) >> 15) * 325   /* +-100 % = +-127 knob steps */
          + ((mod[D_CUTOFF] * 64 * 256) >> 15);                                /* modulation: +-64 steps */
    cut = ab_clamp(cut, 0, 127 * 256);
    {
        int32_t kk = cut >> 8, fr = cut & 255;
        f_t = AB_CUT_F[kk] + (((AB_CUT_F[kk + 1] - AB_CUT_F[kk]) * fr) >> 8);
    }
    st.f_cur += (f_t - st.f_cur) >> 1;
    res = ab_clamp(ab.p[P_RES] + ((mod[D_RES] * 50) >> 15), 0, 100);
    damp = 32767 - res * 32000 / 100;            /* Q14: 2.0 (no resonance) .. ~0.05 (rings, self-oscillates) */

    e0 = st.vca;
    e1 = st.env >> 9;                            /* Q15 */
    st.vca = e1;
    l0 = st.lvl;                                 /* LEVEL: 1 - depth x (1 - lfo) / 2, from 1 down to 1 - depth */
    l1 = ab_clamp(32767 - (dsum_lvl >> 1) + (mod[D_LEVEL] >> 1), 0, 32767);
    st.lvl = l1;
    master = ab.master;

    /* ---- audio rate ---- */
    for (i = 0; i < n; i++) {
        int32_t s, x, hp, nz, e, f2, i0, i1, i2;
        uint32_t r = ab_rand(), p0;
        nz = (int32_t)r >> 16;                   /* white, Q15 */

        /* the oscillators bend each other through their last outputs */
        i0 = ab_fm(inc[0], (((st.o[1] >> 1) * fm) >> 14) + (((st.o[2] >> 1) * xm) >> 14) + ((nz * chaos) >> 15));
        i1 = ab_fm(inc[1], (nz * chaos) >> 15);
        i2 = ab_fm(inc[2], (((st.o[0] >> 1) * xm) >> 14) + ((nz * chaos) >> 15));
        st.o[0] = ab_clamp(ab_osc(w[0], st.ph[0], ab_abs_inc(i0), (ab_abs_inc(i0) >> 15) | 1u, pw), -49151, 49151);
        st.o[1] = ab_clamp(ab_osc(w[1], st.ph[1], ab_abs_inc(i1), (ab_abs_inc(i1) >> 15) | 1u, pw), -49151, 49151);
        st.o[2] = ab_clamp(ab_osc(w[2], st.ph[2], ab_abs_inc(i2), (ab_abs_inc(i2) >> 15) | 1u, pw), -49151, 49151);
        s = (st.o[0] * st.g[0]) >> 15;
        s += (st.o[1] * st.g[1]) >> 15;
        s += (st.o[2] * st.g[2]) >> 15;
        s += (ab_osc(W_SQR, st.sub_ph, sinc, sdiv, 0x80000000u) * st.g[3]) >> 15;
        if (ring)
            s += (((((st.o[0] >> 1) * (st.o[1] >> 1)) >> 13) * ring) >> 15);
        p0 = st.ph[0];
        st.ph[0] += (uint32_t)i0;
        st.ph[1] += (uint32_t)i1;
        st.ph[2] += (uint32_t)i2;
        st.sub_ph += sinc;
        if (sync && i0 > 0 && st.ph[0] < p0) {    /* OSC 1 wrapped: the synced ones restart where it is now */
            uint32_t frac = st.ph[0] >> 16;
            if (sync & 1u)
                st.ph[1] = frac * ratio[1];
            if (sync & 2u)
                st.ph[2] = frac * ratio[2];
        }

        switch (nt) {
        case N_PINK:                             /* Kellet's economy pink: three poles, -3 dB / octave */
            st.pk0 += ((nz * 3245) >> 15) - ((st.pk0 * 77) >> 15);      /* (x 0.99765, as a leak: no overflow) */
            st.pk1 += ((nz * 9716) >> 15) - ((st.pk1 * 1212) >> 15);    /* x 0.963 */
            st.pk2 += ((nz * 34494) >> 15) - ((st.pk2 * 14090) >> 15);  /* x 0.570 */
            nz = (st.pk0 + st.pk1 + st.pk2 + ((nz * 6056) >> 15)) >> 2;
            break;
        case N_BROWN:                            /* leaky integration: -6 dB / octave above ~20 Hz */
            st.brown += (nz >> 4) - (st.brown >> 7);
            st.brown = ab_clamp(st.brown, -98304, 98304);
            nz = st.brown;
            break;
        case N_DIGI:                             /* a 15-bit register, long sequence, clocked by the key */
        case N_METAL: {                          /* the same, short sequence (93 steps): a metallic tone */
            uint32_t q = st.nph;
            st.nph += ninc;
            if (st.nph < q) {
                uint32_t b = (st.lfsr ^ (st.lfsr >> (nt == N_METAL ? 6u : 1u))) & 1u;
                st.lfsr = (st.lfsr >> 1) | (b << 14);
            }
            nz = st.lfsr & 1u ? 14000 : -14000;
            break;
        }
        case N_CRACKLE:                          /* sparse impulses, each a short decaying click */
            if ((r & 0x3FFu) < 2u)
                st.crk = (int32_t)(r & 0x80000000u ? -30000 : 30000);
            st.crk -= st.crk >> 4;
            nz = st.crk;
            break;
        }
        s += (nz * st.g[4]) >> 15;

        /* SVF low-pass, run twice (2x oversampled for stability at high cutoff); the band-pass state saturates,
         * so high resonance growls instead of exploding */
        x = ab_clamp(s >> 1, -65535, 65535);
        for (f2 = 0; f2 < 2; f2++) {
            st.lp += (st.f_cur * st.bp) >> 14;
            hp = ab_clamp(x - st.lp - ((damp * st.bp) >> 14), -131071, 131071);
            st.bp += (st.f_cur * hp) >> 14;
            st.bp = ab_clamp(st.bp, -65535, 65535);
            st.lp = ab_clamp(st.lp, -65535, 65535);
        }
        x = st.lp;

        e = e0 + (((e1 - e0) * (int32_t)i) >> 5);  /* the envelope ramped over the block */
        x = (x * e) >> 15;
        e = l0 + (((l1 - l0) * (int32_t)i) >> 5);  /* LEVEL modulation, ramped too */
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
