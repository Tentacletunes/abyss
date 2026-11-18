/* ABYSS host test: render the engine to WAV files, check levels and stability.
 *   cc -O2 -Ibuild/gen -Ifirmware/dsp host/render.c -o build/render && build/render build/wav */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "abyss_tables.h"
#include "../firmware/dsp/abyss_dsp.c"
#include "../firmware/dsp/abyss_presets.h"

static FILE *wav_open(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fseek(f, 44, SEEK_SET);
    return f;
}
static void wav_close(FILE *f)
{
    uint32_t n = (uint32_t)ftell(f) - 44u, fs = AB_FS;
    uint8_t h[44];
    memcpy(h, "RIFF", 4); *(uint32_t *)(h + 4) = 36 + n; memcpy(h + 8, "WAVEfmt ", 8);
    *(uint32_t *)(h + 16) = 16; *(uint16_t *)(h + 20) = 1; *(uint16_t *)(h + 22) = 2;
    *(uint32_t *)(h + 24) = fs; *(uint32_t *)(h + 28) = fs * 4; *(uint16_t *)(h + 32) = 4; *(uint16_t *)(h + 34) = 16;
    memcpy(h + 36, "data", 4); *(uint32_t *)(h + 40) = n;
    fseek(f, 0, SEEK_SET); fwrite(h, 1, 44, f); fclose(f);
}

static int32_t peak_all;
static double sumsq; static long nsamp;
static void run(FILE *f, double sec)
{
    int32_t buf[2 * AB_BLOCK];
    long blocks = (long)(sec * AB_FS / AB_BLOCK), b;
    for (b = 0; b < blocks; b++) {
        uint32_t i;
        dsp_render(buf, AB_BLOCK);
        for (i = 0; i < 2 * AB_BLOCK; i++) {
            int32_t v = buf[i];
            int16_t s = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
            if (v > 32767 || v < -32768) { fprintf(stderr, "overflow %d\n", v); exit(1); }
            if (abs(v) > peak_all) peak_all = abs(v);
            sumsq += (double)v * v; nsamp++;
            fwrite(&s, 2, 1, f);
        }
    }
}
static void note(int n, int on) { if (on) { ab.note = (uint8_t)n; ab.gate = 1; ab.trig++; } else ab.gate = 0; }
static void report(const char *name)
{
    printf("%-22s peak %6d (%5.1f dBFS)  rms %5.1f dBFS\n", name, peak_all, 20 * __builtin_log10(peak_all / 32768.0 + 1e-9),
           10 * __builtin_log10(sumsq / (nsamp ? nsamp : 1) / (32768.0 * 32768.0) + 1e-12));
    peak_all = 0; sumsq = 0; nsamp = 0;
}

int main(int argc, char **argv)
{
    char p[512];
    const char *dir = argc > 1 ? argv[1] : ".";
    FILE *f;
    int i, k;

    /* 1: the default sound, a phrase on the keys */
    dsp_init();
    snprintf(p, sizeof p, "%s/01_keys.wav", dir); f = wav_open(p);
    { static const int seq[] = {45, 48, 52, 45, 40, 57};
      for (i = 0; i < 6; i++) { note(seq[i], 1); run(f, 0.45); note(0, 0); run(f, 0.25); } }
    run(f, 1.0); wav_close(f); report("01 keys (default)");

    /* 2: each wave alone, A3, OSC 1 only */
    dsp_init();
    snprintf(p, sizeof p, "%s/02_waves.wav", dir); f = wav_open(p);
    ab.p[P_LVL2] = ab.p[P_LVL3] = 0; ab.p[P_CUTOFF] = 127; ab.p[P_RES] = 0; ab.p[P_DETUNE] = 0;
    for (k = 0; k < W_COUNT; k++) { ab.p[P_WAVE1] = (int16_t)k; note(57, 1); run(f, 0.6); note(0, 0); run(f, 0.4); }
    wav_close(f); report("02 waves");

    /* 3: drone, cutoff sweep with high resonance */
    dsp_init();
    snprintf(p, sizeof p, "%s/03_drone_sweep.wav", dir); f = wav_open(p);
    ab.drone = 1; ab.note = 33; ab.p[P_RES] = 92; ab.p[P_SUB] = 60;
    for (k = 0; k <= 127; k++) { ab.p[P_CUTOFF] = (int16_t)k; run(f, 0.03); }
    for (k = 127; k >= 20; k--) { ab.p[P_CUTOFF] = (int16_t)k; run(f, 0.03); }
    ab.drone = 0; run(f, 1.5); wav_close(f); report("03 drone sweep res 92");

    /* 4: noise colours, full level, all oscillators loud: the mixer's worst case */
    dsp_init();
    snprintf(p, sizeof p, "%s/04_noise.wav", dir); f = wav_open(p);
    ab.drone = 1; ab.p[P_LVL1] = ab.p[P_LVL2] = ab.p[P_LVL3] = 0; ab.p[P_CUTOFF] = 127; ab.p[P_RES] = 0;
    ab.p[P_NOISE] = 100; ab.p[P_NTYPE] = N_WHITE; run(f, 1.5); report("04a white noise");
    ab.p[P_NTYPE] = N_PINK; run(f, 1.5); report("04b pink noise");
    ab.p[P_LVL1] = ab.p[P_LVL2] = ab.p[P_LVL3] = ab.p[P_SUB] = 100; ab.p[P_WAVE1] = ab.p[P_WAVE2] = ab.p[P_WAVE3] = W_SQR;
    ab.p[P_RES] = 100; run(f, 1.5); report("04c everything at max");
    wav_close(f);

    /* 5: the user's mockup: OSC 2 +12, NOISE 68 %, MIX 75 % */
    dsp_init();
    snprintf(p, sizeof p, "%s/05_mockup.wav", dir); f = wav_open(p);
    ab.drone = 1; ab.note = 45; ab.p[P_NOISE] = 68; ab.p[P_MIX] = 75; ab.p[P_CUTOFF] = 90; ab.p[P_RES] = 60;
    run(f, 3.0); wav_close(f); report("05 mockup settings");

    /* 6: Milestone 3 - CROSS: FM sweep, cross mod, ring, sync sweep, chaos */
    dsp_init();
    snprintf(p, sizeof p, "%s/06_cross.wav", dir); f = wav_open(p);
    ab.drone = 1; ab.note = 45; ab.p[P_CUTOFF] = 120; ab.p[P_LVL2] = ab.p[P_LVL3] = 0; ab.p[P_WAVE2] = W_SINE;
    ab.p[P_OSC2] = 7;
    for (k = 0; k <= 100; k += 2) { ab.p[P_FM] = (int16_t)k; run(f, 0.04); }
    report("06a FM sweep");
    ab.p[P_FM] = 0; ab.p[P_LVL3] = 60; ab.p[P_OSC3] = -5;
    for (k = 0; k <= 100; k += 2) { ab.p[P_XMOD] = (int16_t)k; run(f, 0.04); }
    report("06b cross mod sweep");
    ab.p[P_XMOD] = 0; ab.p[P_LVL1] = ab.p[P_LVL3] = 0; ab.p[P_RING] = 100; ab.p[P_LVL2] = 0; run(f, 1.0);
    report("06c ring only");
    ab.p[P_RING] = 0; ab.p[P_LVL2] = 90; ab.p[P_WAVE2] = W_SAW; ab.p[P_SYNC] = S_2;
    for (k = 0; k <= 36; k++) { ab.p[P_OSC2] = (int16_t)k; run(f, 0.05); }
    report("06d sync sweep");
    ab.p[P_SYNC] = 0; ab.p[P_LVL1] = ab.p[P_LVL3] = 70; ab.p[P_CHAOS] = 100; run(f, 2.0);
    report("06e chaos 100");
    wav_close(f);

    /* 7: the six noise types, alone, 1 s each */
    dsp_init();
    snprintf(p, sizeof p, "%s/07_noises.wav", dir); f = wav_open(p);
    ab.drone = 1; ab.note = 45; ab.p[P_LVL1] = ab.p[P_LVL2] = ab.p[P_LVL3] = 0; ab.p[P_CUTOFF] = 127; ab.p[P_RES] = 0;
    ab.p[P_NOISE] = 100;
    for (k = 0; k < N_COUNT; k++) {
        char nm[32];
        ab.p[P_NTYPE] = (int16_t)k; run(f, 1.0);
        snprintf(nm, sizeof nm, "07 noise %s", AB_NOISE_NAME[k]); report(nm);
    }
    wav_close(f);

    /* 8: modulation: LFO 1 on the filter, LFO 2 on pitch, random on OSC 2 */
    dsp_init();
    snprintf(p, sizeof p, "%s/08_mod.wav", dir); f = wav_open(p);
    ab.drone = 1; ab.note = 40; ab.p[P_RES] = 70; ab.p[P_CUTOFF] = 70;
    ab.p[P_L1DEPTH] = 80; ab.p[P_L1RATE] = 60; ab.p[P_L1DEST] = D_CUTOFF; run(f, 2.5);
    ab.p[P_L2DEPTH] = 15; ab.p[P_L2RATE] = 75; ab.p[P_L2DEST] = D_PITCH; run(f, 2.0);
    ab.p[P_RDEPTH] = 60; ab.p[P_RRATE] = 85; ab.p[P_RDEST] = D_OSC2; ab.p[P_RSMOOTH] = 30; run(f, 2.5);
    ab.p[P_L1DEST] = D_LEVEL; ab.p[P_L1WAVE] = LW_SQR; ab.p[P_L1RATE] = 90; ab.p[P_L1DEPTH] = 100; run(f, 2.0);
    report("08 modulation");
    wav_close(f);

    /* 9: fuzz: every parameter random, every 50 ms, for 60 s (overflow and stability) */
    dsp_init();
    snprintf(p, sizeof p, "%s/09_fuzz.wav", dir); f = wav_open(p);
    srand(1234);
    for (i = 0; i < 1200; i++) {
        for (k = 0; k < P_COUNT; k++)
            if (rand() % 4 == 0)
                ab.p[k] = (int16_t)(AB_P[k].min + rand() % (AB_P[k].max - AB_P[k].min + 1));
        if (rand() % 10 == 0) { ab.note = (uint8_t)(rand() % 128); ab.trig++; }
        ab.drone = (uint8_t)(rand() % 3 != 0); ab.gate = (uint8_t)(rand() & 1);
        run(f, 0.05);
    }
    wav_close(f); report("09 fuzz 60 s");

    /* 10: the factory presets: a held drone, then a few keys (one file each) */
    for (k = 0; k < (int)AB_NFACTORY; k++) {
        static const int seq[] = {45, 52, 40, 57};
        char nm[64];
        dsp_init();
        ab_preset_values((uint32_t)k, ab.p);
        snprintf(p, sizeof p, "%s/preset_%02d.wav", dir, k + 1); f = wav_open(p);
        note(45, 1); run(f, 3.5); note(0, 0); run(f, 0.6);
        for (i = 0; i < 4; i++) { note(seq[i], 1); run(f, 0.5); note(0, 0); run(f, 0.3); }
        run(f, 1.5);
        wav_close(f);
        snprintf(nm, sizeof nm, "P%02d %s", k + 1, AB_FACTORY[k].name); report(nm);
    }

    /* 11: Milestone 4 - drive types, delays, runaway feedback */
    dsp_init();
    snprintf(p, sizeof p, "%s/11_drive.wav", dir); f = wav_open(p);
    ab.drone = 1; ab.note = 45; ab.p[P_CUTOFF] = 100;
    for (k = 0; k < X_COUNT; k++) {
        char nm[40];
        int d;
        ab.p[P_DTYPE] = (int16_t)k;
        for (d = 0; d <= 100; d += 4) { ab.p[P_DRIVE] = (int16_t)d; run(f, 0.05); }
        snprintf(nm, sizeof nm, "11 drive %s", AB_DTYPE_NAME[k]); report(nm);
    }
    wav_close(f);
    for (k = 0; k < F_COUNT; k++) {               /* filter types */
        char nm[40];
        dsp_init(); ab.drone = 1; ab.note = 45; ab.p[P_FTYPE] = (int16_t)k; ab.p[P_RES] = 60; ab.p[P_CUTOFF] = 80;
        snprintf(p, sizeof p, "%s/12_filter_%d.wav", dir, k); f = wav_open(p); run(f, 1.0); wav_close(f);
        snprintf(nm, sizeof nm, "12 filter %s", AB_FTYPE_NAME[k]); report(nm);
    }
    dsp_init();
    snprintf(p, sizeof p, "%s/13_delays.wav", dir); f = wav_open(p);
    ab.p[P_WET] = 50; ab.p[P_DEC] = 40; ab.p[P_SUS] = 0; ab.p[P_REL] = 40;
    for (i = 0; i < 4; i++) { note(45 + i * 5, 1); run(f, 0.15); note(0, 0); run(f, 0.6); }
    run(f, 2.0); report("13a series echoes");
    ab.p[P_DMODE] = DM_PARALLEL; ab.p[P_PAN1] = -100; ab.p[P_PAN2] = 100; ab.p[P_XFB] = 60;
    note(52, 1); run(f, 0.15); note(0, 0); run(f, 3.0); report("13b parallel ping-pong");
    ab.p[P_FB1] = 110; ab.p[P_FB2] = 110; ab.p[P_XFB] = 100; ab.p[P_WET] = 100; ab.p[P_MOD1] = ab.p[P_MOD2] = 100;
    note(40, 1); run(f, 0.2); note(0, 0);
    for (k = 0; k < 40; k++) { ab.p[P_T1] = (int16_t)(40 + (k * 7) % 80); run(f, 0.15); }
    report("13c runaway, time moved");
    ab.p[P_FB1] = ab.p[P_FB2] = 0; ab.p[P_XFB] = 0; run(f, 3.0); report("13d after: decays to silence");
    wav_close(f);
    return 0;
}
