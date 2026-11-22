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
 * VOICES 1..4: each voice has its own oscillators, filter, envelope (ab_voice_t); the noise, the modulators,
 * DRIVE, the delays and WIDTH are shared and run once on the voices' sum.
 * Samples are Q15 (32768 = full scale) in int32. Control values are recomputed once per block of AB_BLOCK. */
#include "abyss_params.h"

#define AB_BLOCK 32u
#define AB_SCOPE_N 512u
#define AB_NV 4u                 /* voices at most (VOICES: 1 = MONO .. 4) */

typedef struct {
    int16_t p[P_COUNT];          /* written by the UI */
    volatile uint8_t note;       /* MONO: the MIDI note; any mode: the last note played (the screen's FREQUENCY) */
    volatile uint8_t gate;       /* MONO: a key is held */
    volatile uint8_t drone;      /* PLAY: the voice sounds without a key */
    volatile uint8_t trig;       /* MONO: counts note starts: a change retriggers the envelope */
    volatile int16_t master;     /* Q15 output level (the MASTER pot) */
    volatile uint8_t vel;        /* MONO: the note's velocity, 1..127 (keys: 100) */
    volatile int16_t bend;       /* pitch bend, 1/64 semitone */
    /* VOICES 2..4: each voice's note, key, start count, velocity (the UI assigns them) */
    volatile uint8_t vnote[AB_NV], vgate[AB_NV], vtrig[AB_NV], vvel[AB_NV];
    volatile uint8_t vused;      /* bit k: voice k has had a note */
    volatile uint8_t vlead;      /* the voice started last: the envelope that modulates the shared parts */
    volatile uint32_t spq;       /* MIDI clock: samples a quarter note (0: no clock) */
    volatile uint8_t clk_start;  /* counts MIDI START: the LFOs restart on the beat */
} ab_ctl_t;

static ab_ctl_t ab;

typedef struct {                 /* one voice: oscillators, filter, envelope */
    uint32_t ph[3], sub_ph;      /* oscillator phases (free running: an analog drone never restarts) */
    int32_t o[3];                /* the oscillators' last outputs (FM, cross mod) */
    int32_t lp, bp;              /* SVF state */
    int32_t f_cur;               /* SVF coefficient, Q14, smoothed */
    int32_t env;                 /* Q24 */
    int32_t vca;                 /* Q15 level at the end of the last block */
    int32_t pitch;               /* the note with GLIDE, 1/64 semitone (0: not yet) */
    uint8_t stage, last_trig;    /* 0 idle, 1 attack, 2 decay / sustain, 3 release */
} ab_voice_t;

static struct {
    ab_voice_t v[AB_NV];
    uint32_t rng;
    int32_t pk0, pk1, pk2;       /* pink noise poles */
    int32_t g[5];                /* mixer gains, Q15, smoothed: osc 1..3, sub, noise */
    int32_t lvl;                 /* LEVEL modulation at the end of the last block, Q15 */
    int32_t drift[3];            /* CHAOS: each oscillator's slow random wander */
    uint32_t lfo_ph[2];
    int32_t lfo_hold[2];         /* LFO RANDOM: the value of this cycle */
    uint32_t rnd_ph;
    int32_t rnd_tgt, rnd_cur;    /* RANDOM: the sampled value, and where the smoothing is */
    int32_t brown;               /* brown noise integrator */
    uint32_t nph, lfsr;          /* DIGITAL / METAL: the register's clock and the register */
    int32_t crk;                 /* CRACKLE: the decaying impulse */
    int32_t dtone, hold;         /* DRIVE: its tone filter; DECIMATE's held sample */
    uint32_t hold_n;
    uint32_t wc;                 /* delays: samples written (full rate; the lines store every other one) */
    int32_t acc[2];              /* delays: the pair being averaged into one stored sample */
    uint32_t dcur[2];            /* delays: the time now, half-rate samples Q16 (it glides: the PT2399 pitch bend) */
    int32_t ftone[2], dc[2];     /* delays: the feedback path's low-pass and DC blocker */
    uint32_t wob_ph[2];          /* delays: WOBBLE's slow sine */
    int32_t wob_rnd[2];          /* delays: WOBBLE's flutter */
    int16_t haas[512];           /* WIDTH: the dry signal 10 ms ago, to spread it */
    uint32_t haas_w;
    uint8_t clk_seen;            /* the MIDI START count last acted on */
} st;

/* one block's shared signals, per sample: white noise (CHAOS), the noise into the voices, the voices' sum */
static int32_t ab_white[AB_BLOCK], ab_nz[AB_BLOCK], ab_sum[AB_BLOCK];

/* the two delay lines: 32768 samples at FS / 2 each (1.48 s, the top 11 kHz gone: lo-fi, as the PT2399 is) */
#ifndef AB_POOL
#define AB_POOL
#endif
#define AB_DL_N 32768u
static int16_t ab_dl[2][AB_DL_N] AB_POOL;

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
    uint32_t q;
    if (!inc)                                    /* (FM through zero: a stopped phase has no edge) */
        return 0;
    if (ph < inc) {
        q = ph / div;
        t = q > 32767u ? 32767 : (int32_t)q;
        return 2 * t - ((t * t) >> 15) - 32768;
    }
    if (ph > 0u - inc) {
        q = (0u - ph) / div;
        t = q > 32767u ? 32767 : (int32_t)q;
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
static void ab_env_block(ab_voice_t *v, uint32_t gate, uint32_t trig)
{
    if (trig != v->last_trig) {                  /* a new note: from where it is (no click) */
        v->last_trig = (uint8_t)trig;
        v->stage = 1;
    } else if (gate && (v->stage == 0u || v->stage == 3u)) {   /* (a key, or DRONE on, while releasing) */
        v->stage = 1;
    }
    if (!gate && v->stage && v->stage < 3u)
        v->stage = 3;
    switch (v->stage) {
    case 1:
        v->env += (int32_t)AB_ENV_STEP[ab.p[P_ATK]];
        if (v->env >= 1 << 24) {
            v->env = 1 << 24;
            v->stage = 2;
        }
        break;
    case 2: {
        int32_t sus = ab.drone ? 1 << 24 : (ab.p[P_SUS] << 24) / 100;
        v->env -= (((v->env - sus) >> 10) * (int32_t)AB_ENV_K[ab.p[P_DEC]]) >> 6;
        break;
    }
    case 3:
        v->env -= ((v->env >> 10) * (int32_t)AB_ENV_K[ab.p[P_REL]]) >> 6;
        if (v->env < 256) {
            v->env = 0;
            v->stage = 0;
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
    for (i = 0; i < AB_NV; i++)
        ab.vgate[i] = 0;
    ab.vused = 0;
    ab.vlead = 0;
    ab.spq = 0;
    for (i = 0; i < 2u * AB_DL_N; i++)
        ab_dl[i / AB_DL_N][i % AB_DL_N] = 0;
    for (i = 0; i < AB_NV; i++) {
        st.v[i].last_trig = i ? ab.vtrig[i] : ab.trig;
        st.v[i].ph[1] = 0x55555555u + i * 0x2F0E1D3Bu;   /* the oscillators do not start in phase */
        st.v[i].ph[2] = 0xAAAAAAAAu + i * 0x1B3D5A77u;
        st.v[i].ph[0] = i * 0x3C6EF372u;
        st.v[i].f_cur = AB_CUT_F[AB_P[P_CUTOFF].def];
    }
    st.clk_seen = ab.clk_start;
    for (i = 0; i < P_COUNT; i++)
        ab.p[i] = AB_P[i].def;
    ab.note = 69;
    ab.master = 26000;
    ab.vel = 100;
    ab.bend = 0;
    st.rng = 0x2545F491u;
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

/* MIDI CLOCK SYNC on, and a clock coming in */
static uint32_t ab_synced(void) { return ab.p[P_CLOCK] && ab.spq; }

/* an LFO / RANDOM rate knob -> phase increment a block: Hz, or with the clock a note division */
static uint32_t ab_rate_inc(uint32_t rate)
{
    uint32_t blocks;
    if (!ab_synced())
        return AB_LFO_INC[rate];
    blocks = ab.spq * AB_LDIV_U[rate >> 3] / (96u * AB_BLOCK);   /* (<= 2^32 down to 20 BPM) */
    return 0xFFFFFFFFu / (blocks ? blocks : 1u);
}

/* an LFO's value this block, Q15 bipolar; the phase moves one block */
static int32_t ab_lfo(uint32_t k, uint32_t wave, uint32_t rate)
{
    uint32_t ph = st.lfo_ph[k], nph = ph + ab_rate_inc(rate);
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

/* voice k's controls: its note, key, start count and velocity */
static void ab_voice_ctl(uint32_t k, uint32_t nv, uint32_t *note, uint32_t *gate, uint32_t *trig, uint32_t *vel)
{
    if (nv == 1u) {
        *note = ab.note;
        *gate = ab.gate || ab.drone;
        *trig = ab.trig;
        *vel = ab.vel;
    } else {
        uint32_t used = (ab.vused >> k) & 1u;
        *note = used ? ab.vnote[k] : ab.note;
        *gate = ab.vgate[k] || (ab.drone && (used || !k));   /* (DRONE: the voices played, or the first) */
        *trig = ab.vtrig[k];
        *vel = used ? ab.vvel[k] : ab.vel;
    }
}

/* one voice's block, its control values worked out */
typedef struct {
    uint32_t inc[3];
    uint32_t sinc, sdiv, ratio[3], w[3], pw, sync;
    int32_t fm, xm, ring, chaos, damp, e0, e1;
    uint32_t ftype;
} ab_vblk_t;

/* the voice's oscillators, mixer, filter and envelope into ab_sum */
static __attribute__((noinline)) void ab_voice_run(ab_voice_t *v, const ab_vblk_t *c, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t s, x, hp = 0, nz = ab_white[i], e, f2, i0, i1, i2;
        uint32_t p0;

        /* the oscillators bend each other through their last outputs */
        i0 = ab_fm(c->inc[0], (((v->o[1] >> 1) * c->fm) >> 14) + (((v->o[2] >> 1) * c->xm) >> 14) +
                                        ((nz * c->chaos) >> 15));
        i1 = ab_fm(c->inc[1], (nz * c->chaos) >> 15);
        i2 = ab_fm(c->inc[2], (((v->o[0] >> 1) * c->xm) >> 14) + ((nz * c->chaos) >> 15));
        v->o[0] = ab_clamp(ab_osc(c->w[0], v->ph[0], ab_abs_inc(i0), (ab_abs_inc(i0) >> 15) | 1u, c->pw), -49151, 49151);
        v->o[1] = ab_clamp(ab_osc(c->w[1], v->ph[1], ab_abs_inc(i1), (ab_abs_inc(i1) >> 15) | 1u, c->pw), -49151, 49151);
        v->o[2] = ab_clamp(ab_osc(c->w[2], v->ph[2], ab_abs_inc(i2), (ab_abs_inc(i2) >> 15) | 1u, c->pw), -49151, 49151);
        s = (v->o[0] * st.g[0]) >> 15;
        s += (v->o[1] * st.g[1]) >> 15;
        s += (v->o[2] * st.g[2]) >> 15;
        s += (ab_osc(W_SQR, v->sub_ph, c->sinc, c->sdiv, 0x80000000u) * st.g[3]) >> 15;
        if (c->ring)
            s += (((((v->o[0] >> 1) * (v->o[1] >> 1)) >> 13) * c->ring) >> 15);
        p0 = v->ph[0];
        v->ph[0] += (uint32_t)i0;
        v->ph[1] += (uint32_t)i1;
        v->ph[2] += (uint32_t)i2;
        v->sub_ph += c->sinc;
        if (c->sync && i0 > 0 && v->ph[0] < p0) {   /* OSC 1 wrapped: the synced ones restart where it is now */
            uint32_t frac = v->ph[0] >> 16;
            if (c->sync & 1u)
                v->ph[1] = frac * c->ratio[1];
            if (c->sync & 2u)
                v->ph[2] = frac * c->ratio[2];
        }
        s += ab_nz[i];

        /* SVF, run twice (2x oversampled for stability at high cutoff); the band-pass state saturates, so high
         * resonance growls instead of exploding */
        x = ab_clamp(s >> 1, -65535, 65535);
        for (f2 = 0; f2 < 2; f2++) {
            v->lp += (v->f_cur * v->bp) >> 14;
            hp = ab_clamp(x - v->lp - ((c->damp * v->bp) >> 14), -131071, 131071);
            v->bp += (v->f_cur * hp) >> 14;
            v->bp = ab_clamp(v->bp, -65535, 65535);
            v->lp = ab_clamp(v->lp, -65535, 65535);
        }
        x = c->ftype == F_LP ? v->lp : c->ftype == F_BP ? v->bp : ab_clamp(hp, -65535, 65535);

        e = c->e0 + (((c->e1 - c->e0) * (int32_t)i) >> 5);   /* the envelope ramped over the block */
        ab_sum[i] += (x * e) >> 15;
    }
}

/* the destinations each voice's own envelope moves (ENV MOD); the others follow the voice started last */
#define AB_VOICE_DEST(d) ((d) == D_PITCH || (d) == D_OSC2 || (d) == D_CUTOFF || (d) == D_RES || (d) == D_FM)

static void dsp_render(int32_t *out, uint32_t n)        /* n == AB_BLOCK; out: L, R interleaved, Q15 */
{
    static const int8_t SPREAD[AB_NV] = {0, 3, -3, 1};  /* VOICE SPREAD: each voice's detune, x spread / 3 */
    static const int16_t POLY_G[AB_NV + 1u] = {0, 32767, 23170, 18919, 16384};   /* 1 / sqrt(voices) */
    uint32_t i, k, nv, lead, nt, ninc = 0;
    int32_t g_t[5], mixa, mixb, l0, l1, master, mod[D_COUNT], gm[D_COUNT], dsum_lvl, eamt, edst, chaos, noise;
    int32_t venv[AB_NV], pg;
    uint32_t dtype, dhold, dsh, dmode;
    int32_t dg, dtk, ddry, dwet, fb[2], xfb, ltk, wdry, wwet, gl[2], gr[2];
    uint32_t tgt[2];
    int32_t width, haas_k;

    nv = (uint32_t)ab_clamp(ab.p[P_VOICES], 1, (int32_t)AB_NV);
    lead = nv == 1u ? 0u : ab.vlead;
    if (lead >= nv)
        lead = 0;
    if (ab.clk_start != st.clk_seen) {          /* MIDI START: the LFOs and RANDOM begin on the beat */
        st.clk_seen = ab.clk_start;
        st.lfo_ph[0] = st.lfo_ph[1] = st.rnd_ph = 0;
    }

    /* ---- modulation sources ---- */
    for (k = 0; k < D_COUNT; k++)
        mod[k] = 0;
    dsum_lvl = 0;
    {
        static const uint8_t DEPTH[3] = {P_L1DEPTH, P_L2DEPTH, P_RDEPTH}, DEST[3] = {P_L1DEST, P_L2DEST, P_RDEST};
        int32_t v[3];
        v[0] = ab_lfo(0, (uint32_t)ab.p[P_L1WAVE], (uint32_t)ab.p[P_L1RATE]);
        v[1] = ab_lfo(1, (uint32_t)ab.p[P_L2WAVE], (uint32_t)ab.p[P_L2RATE]);
        {                                        /* RANDOM: sample and hold, then a one-pole SMOOTH */
            uint32_t ph = st.rnd_ph;
            st.rnd_ph += ab_rate_inc((uint32_t)ab.p[P_RRATE]);
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
            mod[dst] += (v[k] * d) >> 15;
            if (dst == D_LEVEL)
                dsum_lvl += d;
        }
    }

    /* ---- the voices' envelopes (a modulator too: ENV MOD) ---- */
    eamt = ab.p[P_EAMT] * 32767 / 100;
    edst = ab.p[P_EDEST];
    for (k = 0; k < AB_NV; k++) {
        ab_voice_t *v = &st.v[k];
        if (k < nv) {
            uint32_t note, gate, trig, vel;
            ab_voice_ctl(k, nv, &note, &gate, &trig, &vel);
            ab_env_block(v, gate, trig);
        } else {                                 /* (VOICES turned down: the rest stop) */
            v->stage = 0;
            v->env = 0;
            v->vca = 0;
        }
        venv[k] = ((v->env >> 9) * eamt) >> 15;  /* the envelope's 0..1 times AMOUNT (+-) */
    }
    for (k = 0; k < D_COUNT; k++)
        gm[k] = mod[k];
    gm[edst] += venv[lead];
    if (edst == D_LEVEL)
        dsum_lvl += eamt < 0 ? -eamt : eamt;

    /* ---- shared: CHAOS drift, mixer, LEVEL ---- */
    chaos = ab.p[P_CHAOS];
    for (k = 0; k < 3u; k++) {                   /* CHAOS: three slow, separate random walks */
        int32_t r = (int32_t)ab_rand() >> 16;
        st.drift[k] += (r - st.drift[k]) >> 8;
    }
    nt = (uint32_t)ab.p[P_NTYPE];
    mixa = ab.p[P_MIX] <= 50 ? 32767 : (100 - ab.p[P_MIX]) * 32767 / 50;   /* OSC 1 side */
    mixb = ab.p[P_MIX] >= 50 ? 32767 : ab.p[P_MIX] * 32767 / 50;           /* OSC 2 + 3 side */
    noise = ab_clamp(ab.p[P_NOISE] + ((gm[D_NOISE] * 100) >> 15), 0, 100);
    g_t[0] = (pct_q15(ab.p[P_LVL1]) * mixa) >> 15;
    g_t[1] = (pct_q15(ab.p[P_LVL2]) * mixb) >> 15;
    g_t[2] = (pct_q15(ab.p[P_LVL3]) * mixb) >> 15;
    g_t[3] = (pct_q15(ab.p[P_SUB]) * mixa) >> 15;
    g_t[4] = pct_q15(noise);
    for (k = 0; k < 5u; k++)
        st.g[k] += (g_t[k] - st.g[k]) >> 2;
    l0 = st.lvl;                                 /* LEVEL: 1 - depth x (1 - lfo) / 2, from 1 down to 1 - depth */
    l1 = ab_clamp(32767 - (dsum_lvl >> 1) + (gm[D_LEVEL] >> 1), 0, 32767);
    st.lvl = l1;
    master = ab.master;
    pg = POLY_G[nv];

    /* ---- each voice's block ---- */
    for (i = 0; i < n; i++)
        ab_sum[i] = 0;
    {
        static ab_vblk_t c[AB_NV];
        uint32_t ftype = (uint32_t)ab.p[P_FTYPE], sync = (uint32_t)ab.p[P_SYNC];
        uint32_t pw = (uint32_t)ab.p[P_PW] * 42949673u;   /* % -> phase */
        int32_t det = ab.p[P_DETUNE] * 64 / 100;           /* cents -> 1/64 semitone */
        int32_t d0 = ab_clamp((st.drift[0] * chaos) >> 9, -768, 768);   /* +-12 semitones at most */
        int32_t d1 = ab_clamp((st.drift[1] * chaos) >> 9, -768, 768);
        int32_t d2 = ab_clamp((st.drift[2] * chaos) >> 9, -768, 768);
        int32_t glide = ab.p[P_GLIDE], gk = 4096 / (1 + glide * glide / 16);
        int32_t spread = ab.p[P_VSPREAD] * 64 / 300;
        int32_t vm[D_COUNT];
        for (k = 0; k < nv; k++) {
            ab_voice_t *v = &st.v[k];
            ab_vblk_t *q = &c[k];
            uint32_t note, gate, trig, vel, j;
            int32_t base, tgtp, cut, f_t, res, vg;
            ab_voice_ctl(k, nv, &note, &gate, &trig, &vel);
            (void)gate;
            (void)trig;
            for (j = 0; j < D_COUNT; j++)        /* this voice's modulation: its own envelope where it can */
                vm[j] = gm[j];
            if (AB_VOICE_DEST(edst))
                vm[edst] = mod[edst] + venv[k];

            /* pitch, with GLIDE */
            tgtp = (int32_t)note * 64;
            if (!glide || !v->pitch)
                v->pitch = tgtp;
            else {
                int32_t d = ((tgtp - v->pitch) * gk) >> 12;
                if (!d)
                    d = tgtp > v->pitch ? 1 : tgtp < v->pitch ? -1 : 0;
                v->pitch += d;
            }
            base = v->pitch + ab.p[P_OSC1] * 64 + ((vm[D_PITCH] * 12 * 64) >> 15) + ab.bend + SPREAD[k] * spread;
            q->inc[0] = ab_pitch_inc(base + d0);
            q->inc[1] = ab_pitch_inc(base + ab.p[P_OSC2] * 64 + det + ((vm[D_OSC2] * 12 * 64) >> 15) + d1);
            q->inc[2] = ab_pitch_inc(base + ab.p[P_OSC3] * 64 - det + d2);
            q->sinc = q->inc[0] >> 1;
            for (j = 0; j < 3u; j++) {
                q->w[j] = (uint32_t)ab.p[P_WAVE1 + j];
                q->ratio[j] = q->inc[j] / ((q->inc[0] >> 16) | 1u);   /* slave / master, Q16 */
            }
            q->sdiv = (q->sinc >> 15) | 1u;
            q->pw = pw;
            q->sync = sync;
            q->ftype = ftype;
            q->fm = ab_clamp(ab.p[P_FM] + ((vm[D_FM] * 100) >> 15), 0, 100) * 655;   /* 100 % = index 2 */
            q->xm = ab.p[P_XMOD] * 491;                 /* 100 % = index 1.5 */
            q->ring = pct_q15(ab.p[P_RING]);
            q->chaos = chaos * 82;                      /* noise FM: 100 % = index 0.25 */
            if (k == lead) {                            /* DIGITAL / METAL noise: clocked at 16 x OSC 1 */
                uint32_t i0 = q->inc[0];
                ninc = i0 > 0x0FFFFFFFu ? 0xFFFFFFFFu : i0 << 4;
                if (nt == N_METAL)
                    ninc >>= 1;
            }

            /* filter */
            cut = ab.p[P_CUTOFF] * 256 + ((ab.p[P_ENVF] * (v->env >> 9)) >> 15) * 325   /* +-100 % = +-127 steps */
                  + ((vm[D_CUTOFF] * 64 * 256) >> 15);                                 /* modulation: +-64 */
            cut = ab_clamp(cut, 0, 127 * 256);
            {
                int32_t kk = cut >> 8, fr = cut & 255;
                f_t = AB_CUT_F[kk] + (((AB_CUT_F[kk + 1] - AB_CUT_F[kk]) * fr) >> 8);
            }
            v->f_cur += (f_t - v->f_cur) >> 1;
            res = ab_clamp(ab.p[P_RES] + ((vm[D_RES] * 50) >> 15), 0, 100);
            q->damp = 32767 - res * 32000 / 100;        /* Q14: 2.0 (no resonance) .. ~0.05 (self-oscillates) */

            /* VCA, with VELOCITY: how much a soft note is quieter (0: not at all) */
            vg = 32767 - (ab.p[P_VELS] * (127 - (int32_t)vel) * 32767) / (100 * 127);
            q->e0 = v->vca;
            q->e1 = ((v->env >> 9) * vg) >> 15;
            v->vca = q->e1;
        }

        /* the noise, shared by the voices */
        for (i = 0; i < n; i++) {
            uint32_t r = ab_rand();
            int32_t nz = (int32_t)r >> 16;       /* white, Q15 */
            ab_white[i] = nz;
            switch (nt) {
            case N_PINK:                         /* Kellet's economy pink: three poles, -3 dB / octave */
                st.pk0 += ((nz * 3245) >> 15) - ((st.pk0 * 77) >> 15);      /* (x 0.99765, as a leak) */
                st.pk1 += ((nz * 9716) >> 15) - ((st.pk1 * 1212) >> 15);    /* x 0.963 */
                st.pk2 += ((nz * 34494) >> 15) - ((st.pk2 * 14090) >> 15);  /* x 0.570 */
                nz = (st.pk0 + st.pk1 + st.pk2 + ((nz * 6056) >> 15)) >> 2;
                break;
            case N_BROWN:                        /* leaky integration: -6 dB / octave above ~20 Hz */
                st.brown += (nz >> 4) - (st.brown >> 7);
                st.brown = ab_clamp(st.brown, -98304, 98304);
                nz = st.brown;
                break;
            case N_DIGI:                         /* a 15-bit register, long sequence, clocked by the key */
            case N_METAL: {                      /* the same, short sequence (93 steps): a metallic tone */
                uint32_t q = st.nph;
                st.nph += ninc;
                if (st.nph < q) {
                    uint32_t b = (st.lfsr ^ (st.lfsr >> (nt == N_METAL ? 6u : 1u))) & 1u;
                    st.lfsr = (st.lfsr >> 1) | (b << 14);
                }
                nz = st.lfsr & 1u ? 14000 : -14000;
                break;
            }
            case N_CRACKLE:                      /* sparse impulses, each a short decaying click */
                if ((r & 0x3FFu) < 2u)
                    st.crk = (int32_t)(r & 0x80000000u ? -30000 : 30000);
                st.crk -= st.crk >> 4;
                nz = st.crk;
                break;
            }
            ab_nz[i] = (nz * st.g[4]) >> 15;
        }
        for (k = 0; k < nv; k++)                 /* (a silent voice costs nothing) */
            if (st.v[k].stage || c[k].e0 || c[k].e1)
                ab_voice_run(&st.v[k], &c[k], n);
    }

    /* ---- DRIVE, DELAYS: control rate ---- */
    dtype = (uint32_t)ab.p[P_DTYPE];
    {
        int32_t d = ab_clamp(ab.p[P_DRIVE] + ((gm[D_DRIVE] * 50) >> 15), 0, 100);
        dg = AB_DRIVE_G[d];
        dsh = (uint32_t)(d * 14 / 100);          /* CRUSH: bits taken off, 0 .. 14 */
        dhold = 1u + (uint32_t)(d * d / 200);    /* DECIMATE: one sample kept of 1 .. 51 */
        dtk = AB_TONE_K[ab.p[P_DTONE]] >> 1;     /* Q14 */
        ddry = (100 - ab.p[P_DMIX]) * 32767 / 100;
        dwet = ab.p[P_DMIX] * 32767 / 100;
        if (dtype <= X_FUZZ)                     /* the clippers sit near full scale: back towards the voice's level */
            dwet = dwet * (100 - (d < 40 ? d : 40) - (d > 40 ? (d - 40) / 4 : 0)) / 100;   /* 100: x 0.45 */
    }
    dmode = (uint32_t)ab.p[P_DMODE];
    for (k = 0; k < 2u; k++) {
        static const uint8_t T[2] = {P_T1, P_T2}, FB[2] = {P_FB1, P_FB2}, MD[2] = {P_MOD1, P_MOD2},
                             PN[2] = {P_PAN1, P_PAN2};
        static const uint32_t WOB_INC[2] = {2000000u, 2900000u};   /* ~0.6 and ~0.9 Hz a block */
        int32_t h, w;
        if (ab_synced())                         /* MIDI CLOCK: a note division (half-rate samples) */
            h = (int32_t)(ab.spq * AB_DDIV_U[(uint32_t)ab.p[T[k]] * AB_DDIV_N / 128u] / 192u);
        else
            h = (int32_t)(AB_DTIME_Q16[ab.p[T[k]]] >> 16);
        h += (h * (gm[D_DTIME] >> 1)) >> 15;     /* modulation: +-50 % */
        st.wob_ph[k] += WOB_INC[k];
        st.wob_rnd[k] += (((int32_t)ab_rand() >> 16) - st.wob_rnd[k]) >> 4;
        w = ((AB_SINE[st.wob_ph[k] >> 22] + st.wob_rnd[k]) * ab.p[MD[k]] * 3) >> 8;   /* 100 %: ~ +-2.5 % */
        h += (h * (w >> 2)) >> 15;
        h = ab_clamp(h, 4, (int32_t)AB_DL_N - 800);
        tgt[k] = (uint32_t)h << 16;
        fb[k] = ab_clamp(ab.p[FB[k]] + ((gm[D_FDBK] * 50) >> 15), 0, 110) * 32767 / 100;
        gl[k] = (100 - ab.p[PN[k]]) * 32767 / 200;
        gr[k] = (100 + ab.p[PN[k]]) * 32767 / 200;
    }
    xfb = ab.p[P_XFB] * 229;                     /* 100 % = 0.7 */
    ltk = AB_TONE_K[ab.p[P_DLTONE]] >> 1;        /* Q14 */
    wdry = ab.p[P_WET] <= 50 ? 32767 : (100 - ab.p[P_WET]) * 32767 / 50;
    wwet = ab.p[P_WET] >= 50 ? 32767 : ab.p[P_WET] * 32767 / 50;
    width = ab.p[P_WIDTH];
    haas_k = width > 50 ? (width - 50) * 229 : 0;   /* 100: 0.35 */

    /* ---- audio rate: the voices' sum through LEVEL, DRIVE, the DELAYS, WIDTH ---- */
    for (i = 0; i < n; i++) {
        int32_t x = ab_sum[i], e;
        if (nv > 1u)                             /* (each voice <= 65535: four fit) */
            x = ((x >> 2) * pg) >> 13;
        x = ab_clamp(x, -65535, 65535);
        e = l0 + (((l1 - l0) * (int32_t)i) >> 5);  /* LEVEL modulation, ramped */
        x = (x * e) >> 15;

        /* DRIVE: the pedal after the voice */
        {
            int32_t v = (x * dg) >> 8, y;
            switch (dtype) {
            case X_SOFT: y = ab_softclip(v); break;
            case X_HARD: y = ab_clamp(v, -32767, 32767); break;
            case X_FOLD: {                       /* a triangle fold: over the top, back down */
                int32_t u = (v + 32768) & 0x1FFFF;
                y = u < 65536 ? u - 32768 : 98304 - u;
                break;
            }
            case X_FUZZ:                         /* asymmetric: round on top, flat at the bottom (LM386-ish) */
                y = v > 0 ? ab_softclip(v) : (v < -22000 ? -22000 : v);
                break;
            case X_CRUSH: y = (ab_clamp(x, -32767, 32767) >> dsh) * (1 << dsh); break;
            default:                             /* DECIMATE */
                if (++st.hold_n >= dhold) {
                    st.hold_n = 0;
                    st.hold = x;
                }
                y = st.hold;
                break;
            }
            y = ab_clamp(y, -65535, 65535);
            st.dtone += ((y - st.dtone) * dtk) >> 14;
            x = ((x * ddry) >> 15) + ((st.dtone * dwet) >> 15);
        }

        /* DELAYS: two lines, each with its feedback; SERIES feeds 1 into 2 (APTBS), PARALLEL gives both the
         * voice. CROSS FEEDBACK sends each back into the other. The feedback path is darkened (TONE), DC-blocked
         * and soft-clipped as it is written: above 100 % it runs away into a bounded roar, never past full scale */
        {
            int32_t wv[2], in[2], L, R;
            uint32_t q;
            for (q = 0; q < 2u; q++) {
                uint32_t rp, idx;
                int32_t a, b;
                st.dcur[q] += (uint32_t)((int32_t)(tgt[q] - st.dcur[q]) >> 11);   /* the time glides */
                rp = (st.wc << 15) - st.dcur[q];
                idx = (rp >> 16) & (AB_DL_N - 1u);
                a = ab_dl[q][idx];
                b = ab_dl[q][(idx + 1u) & (AB_DL_N - 1u)];
                wv[q] = a + (((b - a) * (int32_t)((rp >> 1) & 0x7FFFu)) >> 15);
            }
            in[0] = x + ((wv[0] * fb[0]) >> 15) + ((wv[1] * xfb) >> 15);
            in[1] = (dmode == DM_SERIES ? wv[0] : x + ((wv[0] * xfb) >> 15)) + ((wv[1] * fb[1]) >> 15);
            for (q = 0; q < 2u; q++) {
                int32_t v = ab_clamp(in[q], -65535, 65535);
                st.ftone[q] += ((v - st.ftone[q]) * ltk) >> 14;
                v = st.ftone[q] - (st.dc[q] >> 8);       /* (the blocker's state in Q8: no dead band) */
                st.dc[q] += (st.ftone[q] * 256 - st.dc[q]) >> 10;
                st.acc[q] += ab_softclip(v);
                if (st.wc & 1u) {
                    ab_dl[q][(st.wc >> 1) & (AB_DL_N - 1u)] = (int16_t)ab_clamp(st.acc[q] >> 1, -32767, 32767);
                    st.acc[q] = 0;
                }
            }
            st.wc++;
            L = ((x * wdry) >> 15) + (((((wv[0] * gl[0]) >> 15) + ((wv[1] * gl[1]) >> 15)) * wwet) >> 15);
            R = ((x * wdry) >> 15) + (((((wv[0] * gr[0]) >> 15) + ((wv[1] * gr[1]) >> 15)) * wwet) >> 15);
            {   /* WIDTH: 0 mono, 50 as it is, 100 the sides doubled and the dry voice spread by its own echo 10 ms
                 * later, added on one side and taken off the other (it cancels in mono) */
                int32_t mid = (L + R) >> 1, side = (L - R) >> 1, h;
                side = (side * width) / 50;
                st.haas[st.haas_w & 511u] = (int16_t)ab_clamp(x, -32767, 32767);
                h = (st.haas[(st.haas_w - 441u) & 511u] * haas_k) >> 15;
                st.haas_w++;
                L = mid + side + h;
                R = mid - side - h;
            }
            L = (ab_softclip(L) * master) >> 15;
            R = (ab_softclip(R) * master) >> 15;
            out[2u * i] = L;
            out[2u * i + 1u] = R;
            x = (L + R) >> 1;
        }
        if (i & 1u)
            ab_scope[ab_scope_w++ & (AB_SCOPE_N - 1u)] = (int16_t)x;
        if (x < 0)
            x = -x;
        if (x > ab_peak)
            ab_peak = x;
    }
}
