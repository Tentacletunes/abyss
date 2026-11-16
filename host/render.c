/* ABYSS host test: render the engine to WAV files, check levels and stability.
 *   cc -O2 -Ibuild/gen -Ifirmware/dsp host/render.c -o build/render && build/render build/wav */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "abyss_tables.h"
#include "../firmware/dsp/abyss_dsp.c"

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
    return 0;
}
