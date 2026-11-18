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
    P_COUNT
};

enum { W_SINE, W_TRI, W_SAW, W_SQR, W_COUNT };
enum { N_WHITE, N_PINK, N_BROWN, N_DIGI, N_METAL, N_CRACKLE, N_COUNT };
enum { S_OFF, S_2, S_3, S_23, S_COUNT };                       /* hard sync of OSC 2 / 3 to OSC 1 */
enum { LW_SINE, LW_TRI, LW_SQR, LW_SAW, LW_RAND, LW_COUNT };    /* LFO waves */
enum { D_PITCH, D_OSC2, D_CUTOFF, D_RES, D_LEVEL, D_FM, D_NOISE, D_COUNT };   /* modulation destinations */

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
};

typedef struct {
    const char *label;       /* small caps over the large value */
    const char *brief;       /* the same in a small column */
    int16_t min, max, def;
    uint8_t fmt;
    uint8_t fine;            /* 1: one click is one step even when turned fast */
} ab_param_t;

static const ab_param_t AB_P[P_COUNT] = {
    [P_OSC1] = {"FREQUENCY", "OSC 1", -48, 48, 0, FMT_OSC1HZ, 1},
    [P_OSC2] = {"OSC 2", "OSC 2", -48, 48, 12, FMT_SEMI, 1},
    [P_OSC3] = {"OSC 3", "OSC 3", -48, 48, -12, FMT_SEMI, 1},
    [P_NOISE] = {"NOISE", "NOISE", 0, 100, 0, FMT_PCT, 0},
    [P_MIX] = {"MIX", "MIX", 0, 100, 50, FMT_PCT, 0},
    [P_CUTOFF] = {"FILTER", "FILTER", 0, 127, 112, FMT_CUT, 0},
    [P_RES] = {"RESONANCE", "RES", 0, 100, 15, FMT_PCT, 0},
    [P_WAVE1] = {"WAVE 1", "WAV 1", 0, W_COUNT - 1, W_SAW, FMT_WAVE, 1},
    [P_WAVE2] = {"WAVE 2", "WAV 2", 0, W_COUNT - 1, W_SQR, FMT_WAVE, 1},
    [P_WAVE3] = {"WAVE 3", "WAV 3", 0, W_COUNT - 1, W_SAW, FMT_WAVE, 1},
    [P_PW] = {"PULSE WIDTH", "PW", 5, 95, 50, FMT_PCT, 0},
    [P_DETUNE] = {"DETUNE", "DETUNE", 0, 100, 6, FMT_CENT, 0},
    [P_NTYPE] = {"NOISE TYPE", "NOISE", 0, N_COUNT - 1, N_WHITE, FMT_NOISE, 1},
    [P_SUB] = {"SUB OSC", "SUB", 0, 100, 0, FMT_PCT, 0},
    [P_LVL1] = {"LEVEL 1", "LVL 1", 0, 100, 80, FMT_PCT, 0},
    [P_LVL2] = {"LEVEL 2", "LVL 2", 0, 100, 70, FMT_PCT, 0},
    [P_LVL3] = {"LEVEL 3", "LVL 3", 0, 100, 70, FMT_PCT, 0},
    [P_ATK] = {"ATTACK", "ATK", 0, 127, 10, FMT_TIME, 0},
    [P_DEC] = {"DECAY", "DEC", 0, 127, 70, FMT_TIME, 0},
    [P_SUS] = {"SUSTAIN", "SUS", 0, 100, 80, FMT_PCT, 0},
    [P_REL] = {"RELEASE", "REL", 0, 127, 75, FMT_TIME, 0},
    [P_ENVF] = {"ENV > FILTER", "ENV>FLT", -100, 100, 0, FMT_BIPCT, 0},
    [P_FM] = {"FM  OSC 2 > 1", "FM", 0, 100, 0, FMT_PCT, 0},
    [P_XMOD] = {"CROSS MOD  1 <> 3", "X-MOD", 0, 100, 0, FMT_PCT, 0},
    [P_RING] = {"RING MOD  1 x 2", "RING", 0, 100, 0, FMT_PCT, 0},
    [P_SYNC] = {"SYNC TO OSC 1", "SYNC", 0, S_COUNT - 1, S_OFF, FMT_SYNC, 1},
    [P_CHAOS] = {"CHAOS", "CHAOS", 0, 100, 0, FMT_PCT, 0},
    [P_L1RATE] = {"LFO 1 RATE", "RATE 1", 0, 127, 50, FMT_LFOHZ, 0},
    [P_L1DEPTH] = {"LFO 1 DEPTH", "DPT 1", 0, 100, 0, FMT_PCT, 0},
    [P_L2RATE] = {"LFO 2 RATE", "RATE 2", 0, 127, 30, FMT_LFOHZ, 0},
    [P_L2DEPTH] = {"LFO 2 DEPTH", "DPT 2", 0, 100, 0, FMT_PCT, 0},
    [P_RRATE] = {"RANDOM RATE", "RND RATE", 0, 127, 70, FMT_LFOHZ, 0},
    [P_RDEPTH] = {"RANDOM DEPTH", "RND DPT", 0, 100, 0, FMT_PCT, 0},
    [P_RSMOOTH] = {"RANDOM SMOOTH", "SMOOTH", 0, 100, 0, FMT_PCT, 0},
    [P_L1WAVE] = {"LFO 1 WAVE", "WAV 1", 0, LW_COUNT - 1, LW_TRI, FMT_LWAVE, 1},
    [P_L1DEST] = {"LFO 1 TO", "TO 1", 0, D_COUNT - 1, D_CUTOFF, FMT_DEST, 1},
    [P_L2WAVE] = {"LFO 2 WAVE", "WAV 2", 0, LW_COUNT - 1, LW_SINE, FMT_LWAVE, 1},
    [P_L2DEST] = {"LFO 2 TO", "TO 2", 0, D_COUNT - 1, D_PITCH, FMT_DEST, 1},
    [P_RDEST] = {"RANDOM TO", "RND TO", 0, D_COUNT - 1, D_PITCH, FMT_DEST, 1},
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
                                                  "NOISE"};
static const char *const AB_DEST_BRIEF[D_COUNT] = {"PITCH", "OSC2", "FILT", "RES", "LVL", "FM", "NOIS"};
