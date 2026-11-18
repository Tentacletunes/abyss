/* ABYSS: the sound's parameters. Shared by the DSP (read once per block) and the UI (the only writer).
 * Every value is an int16 in its own range (min..max); the descriptor says how the UI shows it. */
#pragma once
#include <stdint.h>

enum {
    /* OSC / 01 */
    P_OSC1, P_OSC2, P_OSC3, P_NOISE, P_MIX, P_CUTOFF, P_RES,
    /* OSC / 02 */
    P_WAVE1, P_WAVE2, P_WAVE3, P_PW, P_DETUNE, P_NTYPE, P_SUB,
    /* OSC / 03 */
    P_LVL1, P_LVL2, P_LVL3,
    /* ENV */
    P_ATK, P_DEC, P_SUS, P_REL, P_ENVF,
    P_COUNT
};

enum { W_SINE, W_TRI, W_SAW, W_SQR, W_COUNT };
enum { N_WHITE, N_PINK, N_COUNT };

enum {                       /* how a value reads on the screen */
    FMT_PCT,                 /* 0..100 -> "68%" */
    FMT_BIPCT,               /* -100..100 -> "+40%" */
    FMT_SEMI,                /* semitones -> "+12" */
    FMT_OSC1HZ,              /* OSC 1's tune shown as the frequency it plays: "440.0 Hz" */
    FMT_CUT,                 /* 0..127 -> "1.20 kHz" */
    FMT_WAVE, FMT_NOISE,     /* names */
    FMT_TIME,                /* 0..127 -> "250 ms", "1.2 s" */
    FMT_CENT,                /* 0..100 -> "8 ct" */
};

typedef struct {
    const char *label;       /* small caps over the large value */
    const char *brief;       /* the same in a small column (<= 9 characters) */
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
    [P_ATK] = {"ATTACK", "ATTACK", 0, 127, 10, FMT_TIME, 0},
    [P_DEC] = {"DECAY", "DECAY", 0, 127, 70, FMT_TIME, 0},
    [P_SUS] = {"SUSTAIN", "SUSTAIN", 0, 100, 80, FMT_PCT, 0},
    [P_REL] = {"RELEASE", "RELEASE", 0, 127, 75, FMT_TIME, 0},
    [P_ENVF] = {"ENV > FILTER", "ENV>FLT", -100, 100, 0, FMT_BIPCT, 0},
};

static const char *const AB_WAVE_NAME[W_COUNT] = {"SINE", "TRIANGLE", "SAW", "SQUARE"};
static const char *const AB_WAVE_BRIEF[W_COUNT] = {"SIN", "TRI", "SAW", "SQR"};
static const char *const AB_NOISE_NAME[N_COUNT] = {"WHITE", "PINK"};
