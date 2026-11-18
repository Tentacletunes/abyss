/* ABYSS: the sound's parameters. Shared by the DSP (read once per block) and the UI (the only writer).
 * Every value is an int16 in its own range (min..max); the descriptor says how the UI shows it.
 * New parameters go at the end of their group; presets (Milestone 5) will store them by name order. */
#pragma once
#include <stdint.h>

enum {
    /* MAIN */
    P_OSC1, P_OSC2, P_OSC3, P_NOISE, P_MIX, P_CUTOFF, P_RES,
    /* SHAPE */
    P_WAVE1, P_WAVE2, P_WAVE3, P_PW, P_DETUNE, P_NTYPE, P_SUB,
    /* LEVELS */
    P_LVL1, P_LVL2, P_LVL3,
    /* ENVELOPE */
    P_ATK, P_DEC, P_SUS, P_REL, P_ENVF,
    /* CROSS (Milestone 3): the oscillators acting on each other */
    P_FM, P_XMOD, P_RING, P_SYNC, P_CHAOS,
    /* MOD (Milestone 3): two LFOs and a random sample and hold */
    P_L1RATE, P_L1DEPTH, P_L2RATE, P_L2DEPTH, P_RRATE, P_RDEPTH, P_RSMOOTH,
    /* ROUTING */
    P_L1WAVE, P_L1DEST, P_L2WAVE, P_L2DEST, P_RDEST,
    /* Milestone 4: filter type; DRIVE; DELAY 1, DELAY 2, DELAYS */
    P_FTYPE,
    P_DTYPE, P_DRIVE, P_DTONE, P_DMIX,
    P_T1, P_FB1, P_MOD1, P_PAN1,
    P_T2, P_FB2, P_MOD2, P_PAN2,
    P_DMODE, P_XFB, P_DLTONE, P_WET,
    P_COUNT
};

enum { W_SINE, W_TRI, W_SAW, W_SQR, W_COUNT };
enum { N_WHITE, N_PINK, N_BROWN, N_DIGI, N_METAL, N_CRACKLE, N_COUNT };
enum { S_OFF, S_2, S_3, S_23, S_COUNT };                       /* hard sync of OSC 2 / 3 to OSC 1 */
enum { LW_SINE, LW_TRI, LW_SQR, LW_SAW, LW_RAND, LW_COUNT };    /* LFO waves */
enum { D_PITCH, D_OSC2, D_CUTOFF, D_RES, D_LEVEL, D_FM, D_NOISE, D_DRIVE, D_DTIME, D_FDBK, D_COUNT };   /* targets */
enum { F_LP, F_BP, F_HP, F_COUNT };                             /* filter types */
enum { X_SOFT, X_HARD, X_FOLD, X_FUZZ, X_CRUSH, X_DECIM, X_COUNT };   /* distortion types */
enum { DM_SERIES, DM_PARALLEL, DM_COUNT };                      /* the delays: one into the other (APTBS), or side by side */

enum {                       /* how a value reads on the screen */
    FMT_PCT,                 /* 0..100 -> "68%" */
    FMT_BIPCT,               /* -100..100 -> "+40%" */
    FMT_SEMI,                /* semitones -> "+12" */
    FMT_OSC1HZ,              /* OSC 1's tune shown as the frequency it plays: "440.0 Hz" */
    FMT_CUT,                 /* 0..127 -> "1.20 kHz" */
    FMT_WAVE, FMT_NOISE,     /* names */
    FMT_TIME,                /* 0..127 -> "250 ms", "1.2 s" */
    FMT_CENT,                /* 0..100 -> "8 ct" */
    FMT_LFOHZ,               /* 0..127 -> "0.25 Hz" */
    FMT_SYNC, FMT_LWAVE, FMT_DEST,   /* names */
    FMT_FTYPE, FMT_DTYPE, FMT_DMODE, /* names */
    FMT_DTIME,               /* 0..127 -> "250 ms", "1.20 s" */
    FMT_PAN,                 /* -100..100 -> "L 40", "C", "R 40" */
};

typedef struct {
    const char *label;       /* small caps over the large value */
    const char *brief;       /* the same in a small column */
    int16_t min, max, def;
    uint8_t fmt;
    uint8_t fine;            /* 1: one click is one step even when turned fast */
} ab_param_t;

static const ab_param_t AB_P[P_COUNT] = {
    [P_OSC1] = {"FREQUENCY", "FREQUENCY", -48, 48, 0, FMT_OSC1HZ, 1},
    [P_OSC2] = {"OSC 2 TUNE", "TUNE", -48, 48, 12, FMT_SEMI, 1},
    [P_OSC3] = {"OSC 3 TUNE", "TUNE", -48, 48, -12, FMT_SEMI, 1},
    [P_NOISE] = {"NOISE LEVEL", "LEVEL", 0, 100, 0, FMT_PCT, 0},
    [P_MIX] = {"MIX", "MIX", 0, 100, 50, FMT_PCT, 0},
    [P_CUTOFF] = {"FILTER CUTOFF", "CUTOFF", 0, 127, 112, FMT_CUT, 0},
    [P_RES] = {"RESONANCE", "RESONANCE", 0, 100, 15, FMT_PCT, 0},
    [P_WAVE1] = {"OSC 1 WAVE", "WAVE", 0, W_COUNT - 1, W_SAW, FMT_WAVE, 1},
    [P_WAVE2] = {"OSC 2 WAVE", "WAVE", 0, W_COUNT - 1, W_SQR, FMT_WAVE, 1},
    [P_WAVE3] = {"OSC 3 WAVE", "WAVE", 0, W_COUNT - 1, W_SAW, FMT_WAVE, 1},
    [P_PW] = {"PULSE WIDTH", "PULSE WIDTH", 5, 95, 50, FMT_PCT, 0},
    [P_DETUNE] = {"DETUNE 2 / 3", "DETUNE 2/3", 0, 100, 6, FMT_CENT, 0},
    [P_NTYPE] = {"NOISE TYPE", "TYPE", 0, N_COUNT - 1, N_WHITE, FMT_NOISE, 1},
    [P_SUB] = {"SUB OSC", "SUB OSC", 0, 100, 0, FMT_PCT, 0},
    [P_LVL1] = {"OSC 1 LEVEL", "LEVEL", 0, 100, 80, FMT_PCT, 0},
    [P_LVL2] = {"OSC 2 LEVEL", "LEVEL", 0, 100, 70, FMT_PCT, 0},
    [P_LVL3] = {"OSC 3 LEVEL", "LEVEL", 0, 100, 70, FMT_PCT, 0},
    [P_ATK] = {"ATTACK", "ATTACK", 0, 127, 10, FMT_TIME, 0},
    [P_DEC] = {"DECAY", "DECAY", 0, 127, 70, FMT_TIME, 0},
    [P_SUS] = {"SUSTAIN", "SUSTAIN", 0, 100, 80, FMT_PCT, 0},
    [P_REL] = {"RELEASE", "RELEASE", 0, 127, 75, FMT_TIME, 0},
    [P_ENVF] = {"ENV > FILTER", "ENV AMOUNT", -100, 100, 0, FMT_BIPCT, 0},
    [P_FM] = {"FM  OSC 2 > 1", "FM 2 > 1", 0, 100, 0, FMT_PCT, 0},
    [P_XMOD] = {"CROSS MOD  1 <> 3", "X-MOD 1 <> 3", 0, 100, 0, FMT_PCT, 0},
    [P_RING] = {"RING MOD  1 x 2", "RING 1 x 2", 0, 100, 0, FMT_PCT, 0},
    [P_SYNC] = {"SYNC TO OSC 1", "SYNC", 0, S_COUNT - 1, S_OFF, FMT_SYNC, 1},
    [P_CHAOS] = {"CHAOS", "CHAOS", 0, 100, 0, FMT_PCT, 0},
    [P_L1RATE] = {"LFO 1 RATE", "RATE", 0, 127, 50, FMT_LFOHZ, 0},
    [P_L1DEPTH] = {"LFO 1 DEPTH", "DEPTH", 0, 100, 0, FMT_PCT, 0},
    [P_L2RATE] = {"LFO 2 RATE", "RATE", 0, 127, 30, FMT_LFOHZ, 0},
    [P_L2DEPTH] = {"LFO 2 DEPTH", "DEPTH", 0, 100, 0, FMT_PCT, 0},
    [P_RRATE] = {"RANDOM RATE", "RATE", 0, 127, 70, FMT_LFOHZ, 0},
    [P_RDEPTH] = {"RANDOM DEPTH", "DEPTH", 0, 100, 0, FMT_PCT, 0},
    [P_RSMOOTH] = {"RANDOM SMOOTH", "SMOOTH", 0, 100, 0, FMT_PCT, 0},
    [P_L1WAVE] = {"LFO 1 WAVE", "WAVE", 0, LW_COUNT - 1, LW_TRI, FMT_LWAVE, 1},
    [P_L1DEST] = {"LFO 1 TARGET", "TARGET", 0, D_COUNT - 1, D_CUTOFF, FMT_DEST, 1},
    [P_L2WAVE] = {"LFO 2 WAVE", "WAVE", 0, LW_COUNT - 1, LW_SINE, FMT_LWAVE, 1},
    [P_L2DEST] = {"LFO 2 TARGET", "TARGET", 0, D_COUNT - 1, D_PITCH, FMT_DEST, 1},
    [P_RDEST] = {"RANDOM TARGET", "TARGET", 0, D_COUNT - 1, D_PITCH, FMT_DEST, 1},
    [P_FTYPE] = {"FILTER TYPE", "TYPE", 0, F_COUNT - 1, F_LP, FMT_FTYPE, 1},
    [P_DTYPE] = {"DRIVE TYPE", "TYPE", 0, X_COUNT - 1, X_SOFT, FMT_DTYPE, 1},
    [P_DRIVE] = {"DRIVE", "DRIVE", 0, 100, 0, FMT_PCT, 0},
    [P_DTONE] = {"DRIVE TONE", "TONE", 0, 100, 85, FMT_PCT, 0},
    [P_DMIX] = {"DRIVE MIX", "MIX", 0, 100, 100, FMT_PCT, 0},
    [P_T1] = {"DELAY 1 TIME", "TIME", 0, 127, 70, FMT_DTIME, 0},
    [P_FB1] = {"DELAY 1 FEEDBACK", "FEEDBACK", 0, 110, 35, FMT_PCT, 0},
    [P_MOD1] = {"DELAY 1 WOBBLE", "WOBBLE", 0, 100, 10, FMT_PCT, 0},
    [P_PAN1] = {"DELAY 1 PAN", "PAN", -100, 100, -50, FMT_PAN, 0},
    [P_T2] = {"DELAY 2 TIME", "TIME", 0, 127, 88, FMT_DTIME, 0},
    [P_FB2] = {"DELAY 2 FEEDBACK", "FEEDBACK", 0, 110, 35, FMT_PCT, 0},
    [P_MOD2] = {"DELAY 2 WOBBLE", "WOBBLE", 0, 100, 10, FMT_PCT, 0},
    [P_PAN2] = {"DELAY 2 PAN", "PAN", -100, 100, 50, FMT_PAN, 0},
    [P_DMODE] = {"DELAY ROUTING", "ROUTING", 0, DM_COUNT - 1, DM_SERIES, FMT_DMODE, 1},
    [P_XFB] = {"CROSS FEEDBACK", "X-FEEDBACK", 0, 100, 0, FMT_PCT, 0},
    [P_DLTONE] = {"DELAY TONE", "TONE", 0, 100, 60, FMT_PCT, 0},
    [P_WET] = {"WET / DRY", "WET/DRY", 0, 100, 0, FMT_PCT, 0},
};

static const char *const AB_WAVE_NAME[W_COUNT] = {"SINE", "TRIANGLE", "SAW", "SQUARE"};
static const char *const AB_WAVE_BRIEF[W_COUNT] = {"SIN", "TRI", "SAW", "SQR"};
static const char *const AB_NOISE_NAME[N_COUNT] = {"WHITE", "PINK", "BROWN", "DIGITAL", "METAL", "CRACKLE"};
static const char *const AB_NOISE_BRIEF[N_COUNT] = {"WHITE", "PINK", "BROWN", "DIGI", "METAL", "CRKL"};
static const char *const AB_SYNC_NAME[S_COUNT] = {"OFF", "OSC 2", "OSC 3", "2 + 3"};
static const char *const AB_SYNC_BRIEF[S_COUNT] = {"OFF", "2", "3", "2+3"};
static const char *const AB_LWAVE_NAME[LW_COUNT] = {"SINE", "TRIANGLE", "SQUARE", "SAW", "RANDOM"};
static const char *const AB_LWAVE_BRIEF[LW_COUNT] = {"SIN", "TRI", "SQR", "SAW", "RND"};
static const char *const AB_DEST_NAME[D_COUNT] = {"PITCH", "OSC 2 PITCH", "FILTER", "RESONANCE", "LEVEL", "FM",
                                                  "NOISE", "DRIVE", "DELAY TIME", "FEEDBACK"};
static const char *const AB_DEST_BRIEF[D_COUNT] = {"PITCH", "OSC 2", "FILTER", "RES", "LEVEL", "FM", "NOISE", "DRIVE",
                                                   "TIME", "FDBK"};
static const char *const AB_FTYPE_NAME[F_COUNT] = {"LOW PASS", "BAND PASS", "HIGH PASS"};
static const char *const AB_FTYPE_BRIEF[F_COUNT] = {"LP", "BP", "HP"};
static const char *const AB_DTYPE_NAME[X_COUNT] = {"SOFT", "HARD", "FOLD", "FUZZ", "CRUSH", "DECIMATE"};
static const char *const AB_DTYPE_BRIEF[X_COUNT] = {"SOFT", "HARD", "FOLD", "FUZZ", "CRUSH", "DECIM"};
static const char *const AB_DMODE_NAME[DM_COUNT] = {"SERIES", "PARALLEL"};
