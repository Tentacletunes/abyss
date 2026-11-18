/* ABYSS factory presets: each is the default sound with these parameters changed. Portable (the host test renders
 * them). Values are in each parameter's own units (abyss_params.h): semitones, %, knob steps 0..127, enum indices. */
#pragma once
#include "abyss_params.h"

typedef struct { int8_t p; int16_t v; } ab_set_t;
typedef struct { const char *name; ab_set_t set[34]; } ab_preset_t;
#define END {-1, 0}

static const ab_preset_t AB_FACTORY[] = {
    {"INDUSTRIAL DRONE", {
        {P_OSC1, -12}, {P_WAVE1, W_SAW}, {P_OSC2, 7}, {P_WAVE2, W_SQR}, {P_OSC3, -12}, {P_WAVE3, W_SAW},
        {P_DETUNE, 18}, {P_SUB, 60}, {P_NOISE, 25}, {P_NTYPE, N_BROWN}, {P_CUTOFF, 72}, {P_RES, 55},
        {P_ATK, 70}, {P_DEC, 90}, {P_SUS, 90}, {P_REL, 105}, {P_CHAOS, 15}, {P_XMOD, 10},
        {P_L1RATE, 30}, {P_L1DEPTH, 35}, {P_L1WAVE, LW_TRI}, {P_L1DEST, D_CUTOFF}, END}},
    {"METALLIC MACHINE", {
        {P_WAVE1, W_SQR}, {P_LVL1, 100}, {P_OSC2, 19}, {P_WAVE2, W_SINE}, {P_LVL2, 90}, {P_FM, 55}, {P_RING, 60}, {P_OSC3, 31}, {P_LVL3, 30},
        {P_NOISE, 20}, {P_NTYPE, N_METAL}, {P_CUTOFF, 105}, {P_RES, 40},
        {P_ATK, 0}, {P_DEC, 55}, {P_SUS, 30}, {P_REL, 60},
        {P_L1RATE, 85}, {P_L1DEPTH, 40}, {P_L1WAVE, LW_SQR}, {P_L1DEST, D_FM}, END}},
    {"BROKEN OSCILLATOR", {
        {P_SYNC, S_2}, {P_WAVE2, W_SAW}, {P_OSC2, 14}, {P_CHAOS, 55}, {P_LVL1, 40}, {P_LVL2, 90}, {P_LVL3, 0},
        {P_CUTOFF, 95}, {P_RES, 30}, {P_ATK, 5}, {P_SUS, 80},
        {P_RRATE, 80}, {P_RDEPTH, 50}, {P_RSMOOTH, 0}, {P_RDEST, D_OSC2}, END}},
    {"FEEDBACK STORM", {
        {P_XMOD, 85}, {P_FM, 40}, {P_RES, 85}, {P_CUTOFF, 80}, {P_NOISE, 30}, {P_NTYPE, N_PINK}, {P_CHAOS, 30},
        {P_SUS, 100}, {P_REL, 100},
        {P_L1RATE, 70}, {P_L1DEPTH, 60}, {P_L1WAVE, LW_SAW}, {P_L1DEST, D_CUTOFF}, END}},
    {"DARK AMBIENT", {
        {P_WAVE1, W_TRI}, {P_OSC2, 12}, {P_WAVE2, W_SINE}, {P_OSC3, -12}, {P_WAVE3, W_TRI}, {P_DETUNE, 25},
        {P_CUTOFF, 52}, {P_RES, 35}, {P_NOISE, 15}, {P_NTYPE, N_PINK}, {P_CHAOS, 8},
        {P_ATK, 105}, {P_DEC, 110}, {P_SUS, 85}, {P_REL, 115},
        {P_L1RATE, 25}, {P_L1DEPTH, 40}, {P_L1WAVE, LW_SINE}, {P_L1DEST, D_CUTOFF},
        {P_L2RATE, 45}, {P_L2DEPTH, 3}, {P_L2WAVE, LW_SINE}, {P_L2DEST, D_PITCH}, END}},
    {"DIGITAL COLLAPSE", {
        {P_NOISE, 60}, {P_NTYPE, N_DIGI}, {P_WAVE1, W_SQR}, {P_PW, 15}, {P_SYNC, S_3}, {P_OSC3, 17}, {P_WAVE3, W_SQR},
        {P_FM, 30}, {P_CUTOFF, 110}, {P_RES, 20}, {P_ATK, 0}, {P_DEC, 45}, {P_SUS, 40}, {P_REL, 50},
        {P_RRATE, 105}, {P_RDEPTH, 70}, {P_RSMOOTH, 0}, {P_RDEST, D_FM}, END}},
    {"NOISE BURST", {
        {P_LVL1, 20}, {P_LVL2, 0}, {P_LVL3, 0}, {P_NOISE, 100}, {P_NTYPE, N_WHITE}, {P_CUTOFF, 95}, {P_RES, 60},
        {P_ENVF, 70}, {P_ATK, 0}, {P_DEC, 72}, {P_SUS, 0}, {P_REL, 62}, END}},
    {"RADIO INTERFERENCE", {
        {P_RING, 70}, {P_NOISE, 60}, {P_NTYPE, N_CRACKLE}, {P_OSC2, 29}, {P_WAVE2, W_SINE}, {P_WAVE1, W_SINE},
        {P_LVL1, 30}, {P_CUTOFF, 100}, {P_RES, 50},
        {P_RRATE, 95}, {P_RDEPTH, 60}, {P_RSMOOTH, 40}, {P_RDEST, D_OSC2},
        {P_L1RATE, 95}, {P_L1DEPTH, 50}, {P_L1WAVE, LW_SQR}, {P_L1DEST, D_LEVEL}, END}},
    {"SUBHARMONIC", {
        {P_OSC1, -12}, {P_WAVE1, W_SINE}, {P_SUB, 100}, {P_OSC2, -24}, {P_WAVE2, W_TRI}, {P_LVL2, 50}, {P_LVL3, 0},
        {P_CUTOFF, 45}, {P_RES, 25}, {P_ENVF, 30}, {P_ATK, 10}, {P_DEC, 80}, {P_SUS, 70}, {P_REL, 80},
        {P_L1RATE, 20}, {P_L1DEPTH, 25}, {P_L1WAVE, LW_SINE}, {P_L1DEST, D_CUTOFF}, END}},
    {"CHAOTIC MACHINE", {
        {P_CHAOS, 80}, {P_XMOD, 50}, {P_FM, 35}, {P_SYNC, S_2}, {P_NOISE, 25}, {P_NTYPE, N_METAL},
        {P_CUTOFF, 85}, {P_RES, 60},
        {P_RRATE, 90}, {P_RDEPTH, 55}, {P_RSMOOTH, 20}, {P_RDEST, D_CUTOFF},
        {P_L1RATE, 75}, {P_L1DEPTH, 50}, {P_L1WAVE, LW_SQR}, {P_L1DEST, D_PITCH},
        {P_L2RATE, 60}, {P_L2DEPTH, 50}, {P_L2WAVE, LW_SAW}, {P_L2DEST, D_RES}, END}},
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
