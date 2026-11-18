/* ABYSS user interface: pages of up to seven parameters on the seven encoders, one value large, three small, a
 * decorative waveform. Big type, few things on screen, one accent colour.
 *
 * Controls (matrix ids from felucca-bsp fm1_input.h; the printed labels as Felucca measured them):
 *   K1..K4, SELECT, ALGORITHM, PRESETS   the page's seven parameters, in that order
 *   MASTER pot                            output level
 *   HOME   OSC pages (again: 01 -> 02 -> 03)     ENV   envelope        EDIT  FILTER
 *   LFO    MOD (Milestone 3)                     FX    FX (Milestone 4) SAVE  PRESETS (Milestone 5)
 *   GLO    performance mode: the last touched value, huge
 *   PLAY   DRONE on / off: the voice sounds without a key
 *   REC    TAP: held, it plays the current note (APTBS's tap pad)
 *   OCT- / OCT+  octave of the keys (both: back to 0)
 *   keys   F3..G5, mono, last note wins; USB MIDI notes on any channel too */

/* ------------------------------------------------------------- palette --- */
#define C_BG RGB(0x10, 0x11, 0x16)
#define C_INK RGB(0xF4, 0xF4, 0xF6)
#define C_DIM RGB(0x8C, 0x8D, 0x96)
#define C_RULE RGB(0x38, 0x39, 0x42)
#define C_ACC RGB(0xBE, 0xA2, 0xF7)       /* lavender: the one accent */

#define MX 14                             /* side margin */

/* ------------------------------------------------------------- pages --- */
enum { PG_OSC1, PG_OSC2, PG_OSC3, PG_ENV, PG_FILTER, PG_MOD, PG_FX, PG_PRESET, PG_COUNT };
typedef struct {
    const char *tag;                      /* "OSC / 01" */
    int8_t slot[7];                       /* K1 K2 K3 K4 SELECT ALGORITHM PRESETS -> parameter, -1 none */
    int8_t prio[7];                       /* the order they are shown in when not focused */
    const char *later;                    /* not built yet: what the page says */
} page_t;
#define NONE7 {-1, -1, -1, -1, -1, -1, -1}
static const page_t PAGES[PG_COUNT] = {
    [PG_OSC1] = {"OSC / 01", {P_OSC1, P_OSC2, P_OSC3, P_NOISE, P_MIX, P_CUTOFF, P_RES},
                 {P_OSC1, P_OSC2, P_NOISE, P_MIX, P_OSC3, P_CUTOFF, P_RES}, 0},
    [PG_OSC2] = {"OSC / 02", {P_WAVE1, P_WAVE2, P_WAVE3, P_PW, P_DETUNE, P_NTYPE, P_SUB},
                 {P_WAVE1, P_WAVE2, P_WAVE3, P_PW, P_DETUNE, P_NTYPE, P_SUB}, 0},
    [PG_OSC3] = {"OSC / 03", {P_LVL1, P_LVL2, P_LVL3, P_SUB, P_NOISE, P_MIX, -1},
                 {P_LVL1, P_LVL2, P_LVL3, P_SUB, P_NOISE, P_MIX, -1}, 0},
    [PG_ENV] = {"ENV / 01", {P_ATK, P_DEC, P_SUS, P_REL, P_ENVF, -1, -1},
                {P_ATK, P_DEC, P_SUS, P_REL, P_ENVF, -1, -1}, 0},
    [PG_FILTER] = {"FILTER / 01", {P_CUTOFF, P_RES, P_ENVF, -1, -1, -1, -1},
                   {P_CUTOFF, P_RES, P_ENVF, -1, -1, -1, -1}, 0},
    [PG_MOD] = {"MOD", NONE7, NONE7, "LFO 1, LFO 2, RANDOM\nARRIVE IN MILESTONE 3"},
    [PG_FX] = {"FX", NONE7, NONE7, "DISTORTION AND DUAL DELAY\nARRIVE IN MILESTONE 4"},
    [PG_PRESET] = {"PRESETS", NONE7, NONE7, "PRESETS ARRIVE\nIN MILESTONE 5"},
};

/* matrix button ids (Felucca panel.c PANEL_DEFAULT, measured on hardware) */
enum { BT_OCTDN = 0, BT_OCTUP = 1, BT_FX = 2, BT_SCL = 3, BT_ENV = 4, BT_LFO = 5, BT_EDIT = 6, BT_GLO = 7,
       BT_HOME = 8, BT_SAVE = 9, BT_ARP = 10, BT_SEQ = 11, BT_PLAY = 12, BT_REC = 13 };
/* matrix encoder -> slot: SELECT 0, ALGORITHM 1, K1..K4 2..5, PRESETS 6 */
static const uint8_t ENC_SLOT[FM1_NENC] = {4, 5, 0, 1, 2, 3, 6};

static struct {
    uint8_t page, perf;
    int8_t focus[PG_COUNT];               /* the parameter shown large on each page */
    int8_t octave;
    uint32_t enc_ms[FM1_NENC];            /* last click: acceleration */
    uint32_t touched_ms;                  /* last value change (performance mode's highlight) */
    uint32_t keys;                        /* the note keys as last seen */
    uint8_t stack[16], nstack;            /* held notes, the last one sounds */
    uint8_t tap;                          /* REC held */
    uint8_t full;                         /* redraw and send everything */
    char sig[4][48];                      /* what each band last showed */
    uint32_t frames;
    int32_t wave_gain;                    /* waveform display gain, Q8 */
} ui;

/* ------------------------------------------------------------- notes --- */
static void voice_update(uint32_t retrig)
{
    if (ui.nstack) {
        ab.note = ui.stack[ui.nstack - 1u];
        if (retrig)
            ab.trig++;
        ab.gate = 1;
    } else {
        ab.gate = ui.tap;
    }
}
static void note_on(uint32_t n)
{
    uint32_t i, j;
    for (i = j = 0; i < ui.nstack; i++)            /* (again: to the top) */
        if (ui.stack[i] != n)
            ui.stack[j++] = ui.stack[i];
    ui.nstack = (uint8_t)j;
    if (ui.nstack == sizeof ui.stack) {            /* full: forget the oldest */
        for (i = 1; i < ui.nstack; i++)
            ui.stack[i - 1u] = ui.stack[i];
        ui.nstack--;
    }
    ui.stack[ui.nstack++] = (uint8_t)n;
    voice_update(1);
}
static void note_off(uint32_t n)
{
    uint32_t i, j, top = ui.nstack && ui.stack[ui.nstack - 1u] == n;
    for (i = j = 0; i < ui.nstack; i++)
        if (ui.stack[i] != n)
            ui.stack[j++] = ui.stack[i];
    ui.nstack = (uint8_t)j;
    if (top)
        voice_update(0);                           /* legato back to the note still held */
}
static uint32_t key_note(uint32_t k) { return 53u + k + (uint32_t)(ui.octave * 12); }   /* key 0 = F3 */

/* ------------------------------------------------------------- input --- */
static void param_step(int32_t pi, int32_t d)
{
    const ab_param_t *p = &AB_P[pi];
    int32_t v = ab.p[pi] + d;
    ab.p[pi] = (int16_t)(v < p->min ? p->min : v > p->max ? p->max : v);
    ui.touched_ms = fm1_ms;
}

static void page_go(uint32_t pg)
{
    ui.page = (uint8_t)pg;
}

static void button(uint32_t b)
{
    switch (b) {
    case BT_HOME: page_go(ui.page == PG_OSC1 ? PG_OSC2 : ui.page == PG_OSC2 ? PG_OSC3 : PG_OSC1); break;
    case BT_ENV: page_go(PG_ENV); break;
    case BT_EDIT: page_go(PG_FILTER); break;
    case BT_LFO: page_go(PG_MOD); break;
    case BT_FX: page_go(PG_FX); break;
    case BT_SAVE: page_go(PG_PRESET); break;
    case BT_GLO: ui.perf ^= 1u; ui.full = 1; break;
    case BT_PLAY: ab.drone ^= 1u; break;
    case BT_REC:
        ui.tap = 1;
        ab.trig++;
        voice_update(0);
        break;
    case BT_OCTDN:
    case BT_OCTUP: {
        uint32_t both = (fm1_in.buttons & 3u) == 3u;
        int32_t o = both ? 0 : ui.octave + (b == BT_OCTUP ? 1 : -1);
        ui.octave = (int8_t)(o < -3 ? -3 : o > 3 ? 3 : o);
        break;
    }
    }
}

static void midi_poll(void)
{
    if (midi_in_overflow) {                        /* a broken stream: drop it, silence its notes */
        mi_r = mi_w;
        midi_in_overflow = 0;
        ui.nstack = 0;
        voice_update(0);
        return;
    }
    while (mi_r != mi_w) {
        uint32_t pkt = midi_in_q[mi_r % MQ], st = (pkt >> 8) & 0xF0u, d1 = (pkt >> 16) & 0x7Fu, d2 = pkt >> 24 & 0x7Fu;
        mi_r++;
        if (st == 0x90u && d2)
            note_on(d1);
        else if (st == 0x80u || st == 0x90u)
            note_off(d1);
        else if (st == 0xB0u && (d1 == 120u || d1 == 123u)) {   /* all sound / notes off */
            ui.nstack = 0;
            voice_update(0);
        }
    }
}

static void ui_poll(void)                      /* keys, buttons, knobs, MIDI: often, also while the screen sends */
{
    uint32_t i, keys = fm1_in.notes, d = keys ^ ui.keys, rel = 0, pressed;
    for (i = 0; d && i < 27u; i++)
        if ((d >> i) & 1u) {
            if ((keys >> i) & 1u)
                note_on(key_note(i));
            else
                note_off(key_note(i));
        }
    ui.keys = keys;
    pressed = fm1_input_edges(&rel);
    for (i = 0; i < 14u; i++)
        if ((pressed >> i) & 1u)
            button(i);
    if (((rel >> BT_REC) & 1u) && ui.tap) {
        ui.tap = 0;
        voice_update(0);
    }
    for (i = 0; i < FM1_NENC; i++) {
        int32_t s = fm1_enc_take(i), pi;
        uint32_t now = fm1_ms, dt;
        if (!s)
            continue;
        pi = PAGES[ui.page].slot[ENC_SLOT[i]];
        if (pi < 0)
            continue;
        dt = now - ui.enc_ms[i];
        ui.enc_ms[i] = now;
        if (!AB_P[pi].fine && AB_P[pi].max - AB_P[pi].min > 40)
            s *= dt < 30u ? 4 : dt < 70u ? 2 : 1;      /* turned fast: bigger steps */
        param_step(pi, s);
        ui.focus[ui.page] = (int8_t)pi;
    }
    midi_poll();
}
static void gfx_poll(void) { ui_poll(); }

static void ui_leds(void)
{
    static const uint8_t PAGE_BTN[PG_COUNT] = {BT_HOME, BT_HOME, BT_HOME, BT_ENV, BT_EDIT, BT_LFO, BT_FX, BT_SAVE};
    uint32_t i;
    for (i = 0; i < 14u; i++) {
        uint32_t on = i == PAGE_BTN[ui.page] || (i == BT_PLAY && ab.drone) || (i == BT_GLO && ui.perf) ||
                      (i == BT_REC && ui.tap) || (i == BT_OCTUP && ui.octave > 0) || (i == BT_OCTDN && ui.octave < 0);
        fm1_led_key(i, (int)on);
    }
    for (i = 0; i < 27u; i++)
        fm1_led_key(14u + i, (int)((ui.keys >> i) & 1u));
}

/* ------------------------------------------------------------- values as text --- */
static char *put_u(char *b, uint32_t v)
{
    char t[10];
    uint32_t n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n)
        *b++ = t[--n];
    *b = 0;
    return b;
}
static char *put_s(char *b, const char *s) { while (*s) *b++ = *s++; *b = 0; return b; }
static char *put_fix(char *b, uint32_t v, uint32_t scale, uint32_t dec)   /* v / scale with dec decimals */
{
    uint32_t i, f, m = 1;
    for (i = 0; i < dec; i++)
        m *= 10u;
    v = (v * m + scale / 2u) / scale;
    b = put_u(b, v / m);
    if (dec) {
        *b++ = '.';
        f = v % m;
        for (i = dec; i--;) {
            uint32_t k = 1;
            uint32_t j;
            for (j = 0; j < i; j++)
                k *= 10u;
            *b++ = (char)('0' + f / k % 10u);
        }
        *b = 0;
    }
    return b;
}
static void hz_text(char *num, char *unit, uint32_t hz10, uint32_t brief)
{
    if (hz10 >= 10000u) {                          /* kHz */
        uint32_t hz = hz10 / 10u;
        put_fix(num, hz, 1000u, brief ? (hz >= 10000u ? 1u : 2u) : (hz >= 10000u ? 1u : 2u));
        put_s(unit, brief ? "kHz" : " kHz");
    } else {
        if (brief || hz10 >= 10000u)
            put_u(num, (hz10 + 5u) / 10u);
        else
            put_fix(num, hz10, 10u, 1u);
        put_s(unit, brief ? "Hz" : " Hz");
    }
}
/* the value of parameter pi as a number and a unit; brief: the small columns */
static void value_text(int32_t pi, char *num, char *unit, uint32_t brief)
{
    int32_t v = ab.p[pi];
    num[0] = unit[0] = 0;
    switch (AB_P[pi].fmt) {
    case FMT_PCT: put_u(num, (uint32_t)v); put_s(unit, "%"); break;
    case FMT_BIPCT:
        if (v > 0) *num++ = '+'; else if (v < 0) { *num++ = '-'; v = -v; }
        put_u(num, (uint32_t)v); put_s(unit, "%");
        break;
    case FMT_SEMI:
        if (v > 0) *num++ = '+'; else if (v < 0) { *num++ = '-'; v = -v; }
        put_u(num, (uint32_t)v);
        if (!brief) put_s(unit, " st");
        break;
    case FMT_OSC1HZ: hz_text(num, unit, dsp_osc1_hz10(), brief); break;
    case FMT_CUT: hz_text(num, unit, AB_CUT_HZ10[v], brief); break;
    case FMT_WAVE: put_s(num, brief ? AB_WAVE_BRIEF[v] : AB_WAVE_NAME[v]); break;
    case FMT_NOISE: put_s(num, AB_NOISE_NAME[v]); break;
    case FMT_TIME: {
        uint32_t ms10 = AB_ENV_MS10[v];
        if (ms10 >= 10000u) { put_fix(num, ms10, 10000u, ms10 >= 100000u ? 0u : 1u); put_s(unit, brief ? "s" : " s"); }
        else if (ms10 >= 100u) { put_u(num, (ms10 + 5u) / 10u); put_s(unit, brief ? "ms" : " ms"); }
        else { put_fix(num, ms10, 10u, 1u); put_s(unit, brief ? "ms" : " ms"); }
        break;
    }
    case FMT_CENT: put_u(num, (uint32_t)v); put_s(unit, brief ? "ct" : " ct"); break;
    }
}

/* ------------------------------------------------------------- drawing --- */
enum { B_HEAD, B_VALUE, B_WAVE, B_BOTTOM };
static const uint8_t BAND[4][2] = {{0, 40}, {40, 106}, {106, 176}, {176, 240}};
static const uint8_t BAND_P[4][2] = {{0, 40}, {40, 150}, {150, 226}, {226, 240}};   /* performance mode */

static int32_t focus_of(uint32_t pg)
{
    int32_t f = ui.focus[pg];
    return f >= 0 ? f : PAGES[pg].prio[0];
}

static void draw_head(uint32_t y)
{
    const char *tag = PAGES[ui.page].tag;
    gfx_text(MX, (int32_t)y + 6, &F_TITLE, "ABYSS", C_INK);
    gfx_text(240 - MX - gfx_text_w(&F_LABEL, tag), (int32_t)y + 11, &F_LABEL, tag, C_DIM);
    if (ab.drone)                                  /* DRONE: a small accent mark beside the title */
        gfx_fill(MX + gfx_text_w(&F_TITLE, "ABYSS") + 8, (int32_t)y + 17, 6, 6, C_ACC);
}

static void draw_lines(int32_t x, int32_t y, const char *s, const ab_font_t *f, uint16_t c, int32_t lh)
{
    char line[48];
    while (*s) {
        uint32_t n = 0;
        while (*s && *s != '\n' && n < sizeof line - 1u)
            line[n++] = *s++;
        line[n] = 0;
        if (*s == '\n')
            s++;
        gfx_text(x, y, f, line, c);
        y += lh;
    }
}

static void draw_value(uint32_t y0)
{
    char num[16], unit[8];
    int32_t pi;
    const page_t *pg = &PAGES[ui.page];
    if (pg->later) {
        draw_lines(MX, (int32_t)y0 + 10, pg->later, &F_LABEL, C_DIM, 18);
        return;
    }
    pi = focus_of(ui.page);
    value_text(pi, num, unit, 0);
    gfx_text(MX, (int32_t)y0 + 6, &F_LABEL, AB_P[pi].label, C_DIM);
    if (ui.perf) {                                 /* the number huge, the unit beside it */
        const char *s;
        int32_t x, ok = 1;
        for (s = num; *s; s++)
            ok &= F_HUGE.g[(uint8_t)*s - 32u].adv != 0;
        if (ok && gfx_text_w(&F_HUGE, num) + gfx_text_w(&F_MID, unit) <= 240 - 2 * MX) {
            x = gfx_text(MX - 3, (int32_t)y0 + 14, &F_HUGE, num, C_INK);
            gfx_text(x + 3, (int32_t)y0 + 14 + F_HUGE.asc - F_MID.asc - 12, &F_MID, unit, C_ACC);
        } else {
            x = gfx_text(MX - 1, (int32_t)y0 + 34, &F_BIG, num, C_INK);
            gfx_text(x, (int32_t)y0 + 34, &F_BIG, unit, C_INK);
        }
        return;
    }
    {
        int32_t x = gfx_text(MX - 1, (int32_t)y0 + 18, &F_BIG, num, C_INK);
        gfx_text(x, (int32_t)y0 + 18, &F_BIG, unit, C_INK);
    }
}

/* the small values under the waveform. OSC pages: K1..K4 in the knobs' order, the one turned last in the accent;
 * other pages: three of the rest, by the page's priority. Returns how many */
static int32_t bottom_list(uint32_t pg_i, int32_t *out)
{
    const page_t *pg = &PAGES[pg_i];
    int32_t f = focus_of(pg_i), n = 0, i;
    if (pg_i <= PG_OSC3) {
        for (i = 0; i < 4; i++)
            if (pg->slot[i] >= 0)
                out[n++] = pg->slot[i];
        return n;
    }
    for (i = 0; i < 7 && n < 3; i++)
        if (pg->prio[i] >= 0 && pg->prio[i] != f)
            out[n++] = pg->prio[i];
    return n;
}

static void draw_bottom(uint32_t y0)
{
    const page_t *pg = &PAGES[ui.page];
    int32_t f = focus_of(ui.page), list[4], n, i, quad = ui.page <= PG_OSC3, cw = 76;
    const ab_font_t *vf = quad ? &F_VAL : &F_MID;
    if (pg->later)
        return;
    n = bottom_list(ui.page, list);
    gfx_fill(MX, (int32_t)y0 + 2, 240 - 2 * MX, 1, C_RULE);
    for (i = 0; i < n; i++) {
        int32_t pi = list[i], x = quad ? MX + (i ? 6 + i * 52 : 0) : MX + i * cw;   /* (quad: the first column wider, for "440Hz") */
        char num[16], unit[8];
        value_text(pi, num, unit, 1);
        gfx_text(x, (int32_t)y0 + 13, &F_LABEL, AB_P[pi].brief, quad && pi == f ? C_ACC : C_DIM);
        x = gfx_text(x - 1, (int32_t)y0 + (quad ? 30 : 27), vf, num, C_INK);
        gfx_text(x + 1, (int32_t)y0 + 37, &F_LABEL, unit, C_DIM);
    }
}

/* the waveform: the output from its last rising zero crossing, a point every 6 px, a 3 px stroke */
static void draw_wave(uint32_t y0, uint32_t y1)
{
    enum { NPT = 36, STEP = 5 };
    int32_t pts[NPT + 1], i, peak = 0, mid = (int32_t)(y0 + y1) / 2, amp = (int32_t)(y1 - y0) / 2 - 6, g;
    uint32_t w = ab_scope_w, start = w - (uint32_t)(NPT * STEP) - 1u, k;
    for (k = 0; k < 200u; k++) {                   /* a rising zero crossing, for a still picture */
        uint32_t a = start - k;
        if (ab_scope[(a - 1u) & (AB_SCOPE_N - 1u)] < 0 && ab_scope[a & (AB_SCOPE_N - 1u)] >= 0) {
            start = a;
            break;
        }
    }
    for (i = 0; i <= NPT; i++) {
        int32_t v = ab_scope[(start + (uint32_t)(i * STEP)) & (AB_SCOPE_N - 1u)];
        pts[i] = v;
        if (v < 0) v = -v;
        if (v > peak) peak = v;
    }
    g = (amp << 15) / (peak > 3000 ? peak : 3000);  /* quiet sounds stay small, silence a flat line */
    ui.wave_gain += (g - ui.wave_gain) / 3;
    for (i = 0; i < NPT; i++) {
        int32_t xa = MX + i * 6, xb = MX + (i + 1) * 6;
        int32_t ya = mid - ((pts[i] * ui.wave_gain) >> 15), yb = mid - ((pts[i + 1] * ui.wave_gain) >> 15);
        gfx_line(xa, ya, xb > 240 - MX ? 240 - MX : xb, yb, 3, C_ACC);
    }
}

/* each band: a signature of what it shows; redrawn and sent only when that changes (the waveform: every frame) */
static void band_sig(uint32_t b, char *s)
{
    char *p = s;
    const page_t *pg = &PAGES[ui.page];
    int32_t f = focus_of(ui.page), i;
    *p++ = (char)('A' + ui.page);
    *p++ = (char)('0' + ui.perf);
    if (b == B_HEAD) {
        *p++ = (char)('0' + ab.drone);
    } else if (b == B_VALUE && !pg->later) {
        char num[16], unit[8];
        *p++ = (char)('a' + f);
        value_text(f, num, unit, 0);
        p = put_s(p, num);
        p = put_s(p, unit);
    } else if (b == B_BOTTOM && !pg->later) {
        char num[16], unit[8];
        int32_t list[4], n = bottom_list(ui.page, list);
        *p++ = (char)('a' + f);
        for (i = 0; i < n; i++) {
            value_text(list[i], num, unit, 1);
            p = put_s(p, num);
            *p++ = '|';
        }
    }
    *p = 0;
}

static void ui_frame(void)
{
    uint32_t b;
    const uint8_t (*bands)[2] = ui.perf ? BAND_P : BAND;
    char sig[48];
    ui_leds();
    for (b = 0; b < 4u; b++) {
        uint32_t y0 = bands[b][0], y1 = bands[b][1], i, same;
        if (b == B_BOTTOM && ui.perf)
            continue;                              /* performance mode: no small values */
        band_sig(b, sig);
        same = !ui.full && b != B_WAVE;
        for (i = 0; same && i < sizeof sig; i++) {
            if (sig[i] != ui.sig[b][i])
                same = 0;
            if (!sig[i])
                break;
        }
        if (same)
            continue;
        for (i = 0; i < sizeof sig; i++)
            ui.sig[b][i] = sig[i];
        gfx_fill(0, (int32_t)y0, 240, (int32_t)(y1 - y0), C_BG);
        switch (b) {
        case B_HEAD: draw_head(y0); break;
        case B_VALUE: draw_value(y0); break;
        case B_WAVE: draw_wave(y0 + 4u, y1 - 4u); break;
        case B_BOTTOM: draw_bottom(y0); break;
        }
        gfx_flush_rows(y0, y1);
    }
    if (ui.perf && ui.full) {                      /* (the strip under the waveform) */
        gfx_fill(0, 226, 240, 14, C_BG);
        gfx_flush_rows(226, 240);
    }
    ui.full = 0;
    ui.frames++;
}

static void ui_init(void)
{
    uint32_t i;
    for (i = 0; i < PG_COUNT; i++)
        ui.focus[i] = -1;
    ui.full = 1;
    ui.wave_gain = 0;
}
