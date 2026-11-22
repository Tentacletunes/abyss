/* ABYSS factory presets: each is the default sound with these parameters changed. Portable (the host test renders
 * them). Values are in each parameter's own units (abyss_params.h): semitones, %, knob steps 0..127, enum indices. */
#pragma once
#include "abyss_params.h"

typedef struct { int8_t p; int16_t v; } ab_set_t;
typedef struct { const char *name; ab_set_t set[48]; } ab_preset_t;
#define END {-1, 0}

static const ab_preset_t AB_FACTORY[] = {
    {"INDUSTRIAL DRONE", {
        {P_OSC1, -12}, {P_WAVE1, W_SAW}, {P_OSC2, 7}, {P_WAVE2, W_SQR}, {P_OSC3, -12}, {P_WAVE3, W_SAW},
        {P_DETUNE, 18}, {P_SUB, 60}, {P_NOISE, 25}, {P_NTYPE, N_BROWN}, {P_CUTOFF, 72}, {P_RES, 55},
        {P_ATK, 70}, {P_DEC, 90}, {P_SUS, 90}, {P_REL, 105}, {P_CHAOS, 15}, {P_XMOD, 10},
        {P_L1RATE, 30}, {P_L1DEPTH, 35}, {P_L1WAVE, LW_TRI}, {P_L1DEST, D_CUTOFF}, {P_DTYPE, X_SOFT}, {P_DRIVE, 30}, {P_DTONE, 60}, {P_WET, 32}, {P_T1, 72}, {P_FB1, 50}, {P_T2, 95}, {P_FB2, 40}, {P_DLTONE, 45}, {P_MOD1, 20}, {P_LVL1, 50}, {P_LVL2, 40}, {P_LVL3, 40}, END}},
    {"METALLIC MACHINE", {
        {P_OSC1, -24}, {P_WAVE1, W_SQR}, {P_LVL1, 100}, {P_OSC2, 19}, {P_WAVE2, W_SINE}, {P_LVL2, 90}, {P_FM, 55}, {P_RING, 60}, {P_OSC3, 31}, {P_LVL3, 30},
        {P_NOISE, 65}, {P_NTYPE, N_METAL}, {P_CUTOFF, 105}, {P_RES, 40},
        {P_ATK, 100}, {P_DEC, 90}, {P_SUS, 90}, {P_REL, 110},
        {P_L1RATE, 85}, {P_L1DEPTH, 40}, {P_L1WAVE, LW_SQR}, {P_L1DEST, D_FM}, {P_DTYPE, X_FOLD}, {P_DRIVE, 70}, {P_DMIX, 100}, {P_WET, 28}, {P_DMODE, DM_PARALLEL}, {P_T1, 30}, {P_T2, 42}, {P_FB1, 45}, {P_FB2, 45}, {P_PAN1, -90}, {P_PAN2, 90}, END}},
    {"BROKEN OSC", {
        {P_SYNC, S_2}, {P_WAVE2, W_SAW}, {P_OSC2, 14}, {P_CHAOS, 55}, {P_LVL1, 40}, {P_LVL2, 90}, {P_LVL3, 0},
        {P_CUTOFF, 95}, {P_RES, 30}, {P_ATK, 5}, {P_SUS, 80},
        {P_RRATE, 80}, {P_RDEPTH, 50}, {P_RSMOOTH, 0}, {P_RDEST, D_OSC2}, {P_DTYPE, X_CRUSH}, {P_DRIVE, 45}, {P_DMIX, 70}, {P_WET, 22}, {P_T1, 55}, {P_FB1, 40}, {P_MOD1, 70}, END}},
    {"FEEDBACK STORM", {
        {P_XMOD, 85}, {P_FM, 40}, {P_RES, 85}, {P_CUTOFF, 80}, {P_NOISE, 30}, {P_NTYPE, N_PINK}, {P_CHAOS, 30},
        {P_SUS, 100}, {P_REL, 100},
        {P_L1RATE, 27}, {P_L1DEPTH, 100}, {P_L1WAVE, LW_SINE}, {P_L1DEST, D_RES}, {P_DTYPE, X_FUZZ}, {P_DRIVE, 100}, {P_DTONE, 95}, {P_WET, 65}, {P_T1, 50}, {P_T2, 66}, {P_FB1, 96}, {P_FB2, 92}, {P_XFB, 55}, {P_MOD1, 1}, {P_MOD2, 1}, {P_DLTONE, 50}, {P_LVL1, 50}, {P_LVL2, 45}, {P_LVL3, 45}, END}},
    {"DARK AMBIENT", {
        {P_WAVE1, W_TRI}, {P_OSC2, 12}, {P_WAVE2, W_SINE}, {P_OSC3, -12}, {P_WAVE3, W_TRI}, {P_DETUNE, 25},
        {P_CUTOFF, 52}, {P_RES, 35}, {P_NOISE, 15}, {P_NTYPE, N_PINK}, {P_CHAOS, 8},
        {P_ATK, 105}, {P_DEC, 110}, {P_SUS, 85}, {P_REL, 115},
        {P_L1RATE, 25}, {P_L1DEPTH, 40}, {P_L1WAVE, LW_SINE}, {P_L1DEST, D_CUTOFF},
        {P_L2RATE, 45}, {P_L2DEPTH, 3}, {P_L2WAVE, LW_SINE}, {P_L2DEST, D_PITCH}, {P_WET, 58}, {P_T1, 100}, {P_T2, 116}, {P_FB1, 72}, {P_FB2, 66}, {P_DLTONE, 30}, {P_MOD1, 30}, {P_MOD2, 35}, {P_PAN1, -80}, {P_PAN2, 80}, END}},
    {"DIGITAL COLLAPSE", {
        {P_NOISE, 60}, {P_NTYPE, N_DIGI}, {P_WAVE1, W_SQR}, {P_PW, 15}, {P_SYNC, S_3}, {P_OSC3, 17}, {P_WAVE3, W_SQR},
        {P_FM, 30}, {P_CUTOFF, 110}, {P_RES, 20}, {P_ATK, 0}, {P_DEC, 45}, {P_SUS, 40}, {P_REL, 50},
        {P_RRATE, 105}, {P_RDEPTH, 70}, {P_RSMOOTH, 0}, {P_RDEST, D_FM}, {P_DTYPE, X_DECIM}, {P_DRIVE, 50}, {P_WET, 30}, {P_T1, 38}, {P_FB1, 60}, {P_DLTONE, 85}, END}},
    {"NOISE BURST", {
        {P_LVL1, 20}, {P_LVL2, 0}, {P_LVL3, 0}, {P_NOISE, 100}, {P_NTYPE, N_WHITE}, {P_CUTOFF, 95}, {P_RES, 60},
        {P_ENVF, 70}, {P_ATK, 0}, {P_DEC, 72}, {P_SUS, 0}, {P_REL, 62}, {P_DTYPE, X_HARD}, {P_DRIVE, 25}, {P_WET, 40}, {P_T1, 58}, {P_T2, 80}, {P_FB1, 50}, {P_FB2, 45}, END}},
    {"RADIO INT", {
        {P_RING, 70}, {P_NOISE, 60}, {P_NTYPE, N_CRACKLE}, {P_OSC2, 29}, {P_WAVE2, W_SINE}, {P_WAVE1, W_SINE},
        {P_LVL1, 30}, {P_CUTOFF, 100}, {P_RES, 50},
        {P_RRATE, 95}, {P_RDEPTH, 60}, {P_RSMOOTH, 40}, {P_RDEST, D_OSC2},
        {P_L1RATE, 95}, {P_L1DEPTH, 50}, {P_L1WAVE, LW_SQR}, {P_L1DEST, D_LEVEL}, {P_DTYPE, X_CRUSH}, {P_DRIVE, 30}, {P_DTONE, 55}, {P_WET, 35}, {P_T1, 64}, {P_FB1, 55}, {P_MOD1, 65}, {P_MOD2, 50}, END}},
    {"SUBHARMONIC", {
        {P_OSC1, -12}, {P_WAVE1, W_SINE}, {P_SUB, 100}, {P_OSC2, -24}, {P_WAVE2, W_TRI}, {P_LVL1, 55}, {P_LVL2, 40}, {P_LVL3, 0},
        {P_CUTOFF, 45}, {P_RES, 25}, {P_ENVF, 30}, {P_ATK, 10}, {P_DEC, 80}, {P_SUS, 70}, {P_REL, 80},
        {P_L1RATE, 20}, {P_L1DEPTH, 25}, {P_L1WAVE, LW_SINE}, {P_L1DEST, D_CUTOFF}, {P_DTYPE, X_SOFT}, {P_DRIVE, 25}, {P_DTONE, 40}, {P_WET, 15}, {P_T1, 90}, {P_FB1, 35}, {P_DLTONE, 35}, END}},
    {"CHAOTIC MACHINE", {
        {P_CHAOS, 80}, {P_XMOD, 50}, {P_FM, 35}, {P_SYNC, S_2}, {P_NOISE, 25}, {P_NTYPE, N_METAL},
        {P_CUTOFF, 85}, {P_RES, 60},
        {P_RRATE, 90}, {P_RDEPTH, 55}, {P_RSMOOTH, 20}, {P_RDEST, D_CUTOFF},
        {P_L1RATE, 75}, {P_L1DEPTH, 50}, {P_L1WAVE, LW_SQR}, {P_L1DEST, D_PITCH},
        {P_L2RATE, 60}, {P_L2DEPTH, 50}, {P_L2WAVE, LW_SAW}, {P_L2DEST, D_RES}, {P_DTYPE, X_FUZZ}, {P_DRIVE, 50}, {P_WET, 45}, {P_DMODE, DM_PARALLEL}, {P_XFB, 40}, {P_FB1, 70}, {P_FB2, 65}, {P_T1, 48}, {P_T2, 61}, {P_LVL1, 50}, {P_LVL2, 45}, {P_LVL3, 45}, END}},
    /* v1.0: VOICES, GLIDE, ENV MOD, WIDTH */
    {"GHOST CHOIR", {
        {P_VOICES, 4}, {P_VSPREAD, 30}, {P_WAVE1, W_SAW}, {P_OSC2, 12}, {P_WAVE2, W_TRI}, {P_OSC3, 7}, {P_WAVE3, W_SAW},
        {P_DETUNE, 14}, {P_LVL1, 55}, {P_LVL2, 45}, {P_LVL3, 35}, {P_NOISE, 12}, {P_NTYPE, N_PINK},
        {P_CUTOFF, 60}, {P_RES, 30}, {P_ENVF, 35}, {P_ATK, 95}, {P_DEC, 100}, {P_SUS, 80}, {P_REL, 108},
        {P_L1RATE, 22}, {P_L1DEPTH, 30}, {P_L1WAVE, LW_SINE}, {P_L1DEST, D_CUTOFF}, {P_CHAOS, 6},
        {P_WET, 55}, {P_DMODE, DM_PARALLEL}, {P_T1, 96}, {P_T2, 110}, {P_FB1, 62}, {P_FB2, 58}, {P_DLTONE, 35},
        {P_MOD1, 35}, {P_MOD2, 40}, {P_PAN1, -100}, {P_PAN2, 100}, {P_WIDTH, 95}, END}},
    {"ACID TUNNEL", {
        {P_GLIDE, 40}, {P_OSC1, -12}, {P_WAVE1, W_SAW}, {P_LVL1, 90}, {P_LVL2, 0}, {P_LVL3, 0}, {P_SUB, 40},
        {P_CUTOFF, 38}, {P_RES, 82}, {P_ENVF, 75}, {P_ATK, 0}, {P_DEC, 55}, {P_SUS, 20}, {P_REL, 50},
        {P_EDEST, D_RES}, {P_EAMT, 30}, {P_VELS, 60},
        {P_DTYPE, X_FUZZ}, {P_DRIVE, 55}, {P_DTONE, 70}, {P_WET, 30}, {P_T1, 62}, {P_T2, 75}, {P_FB1, 55},
        {P_FB2, 40}, {P_MOD1, 15}, {P_WIDTH, 70}, END}},
    {"BROKEN CHORDS", {
        {P_VOICES, 3}, {P_VSPREAD, 60}, {P_WAVE1, W_SQR}, {P_PW, 30}, {P_OSC2, 12}, {P_WAVE2, W_SQR}, {P_LVL3, 0},
        {P_NOISE, 20}, {P_NTYPE, N_CRACKLE}, {P_CUTOFF, 92}, {P_RES, 35}, {P_ATK, 0}, {P_DEC, 70}, {P_SUS, 25}, {P_REL, 70},
        {P_EDEST, D_PITCH}, {P_EAMT, -25}, {P_RRATE, 70}, {P_RDEPTH, 25}, {P_RSMOOTH, 30}, {P_RDEST, D_CUTOFF},
        {P_DTYPE, X_CRUSH}, {P_DRIVE, 40}, {P_DMIX, 75}, {P_WET, 45}, {P_DMODE, DM_PARALLEL}, {P_T1, 58},
        {P_T2, 72}, {P_FB1, 55}, {P_FB2, 50}, {P_XFB, 25}, {P_MOD1, 55}, {P_MOD2, 45}, {P_PAN1, -100}, {P_PAN2, 100},
        {P_WIDTH, 90}, END}},
    {"FM BELLS", {
        {P_VOICES, 4}, {P_WAVE1, W_SINE}, {P_OSC2, 19}, {P_WAVE2, W_SINE}, {P_LVL1, 100}, {P_LVL2, 0}, {P_LVL3, 45},
        {P_WAVE3, W_SINE}, {P_OSC3, 24}, {P_FM, 20}, {P_CUTOFF, 118}, {P_RES, 10}, {P_ATK, 0}, {P_DEC, 98}, {P_SUS, 15}, {P_DTYPE, X_SOFT}, {P_DRIVE, 35},
        {P_REL, 100}, {P_EDEST, D_FM}, {P_EAMT, 70}, {P_VELS, 50}, {P_L2RATE, 60}, {P_L2DEPTH, 2}, {P_L2WAVE, LW_SINE},
        {P_L2DEST, D_PITCH}, {P_WET, 40}, {P_DMODE, DM_PARALLEL}, {P_T1, 80}, {P_T2, 98}, {P_FB1, 55}, {P_FB2, 50},
        {P_DLTONE, 60}, {P_PAN1, -70}, {P_PAN2, 70}, {P_WIDTH, 80}, END}},
};
#define AB_NFACTORY ((uint32_t)(sizeof AB_FACTORY / sizeof AB_FACTORY[0]))

static void ab_preset_values(uint32_t k, int16_t *p)   /* factory preset k as a full parameter set */
{
    const ab_set_t *s;
    uint32_t i;
    for (i = 0; i < P_COUNT; i++)
        p[i] = AB_P[i].def;
    for (s = AB_FACTORY[k].set; s->p >= 0; s++)
        p[s->p] = s->v;
}
