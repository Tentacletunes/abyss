/* ABYSS user interface: pages of up to seven parameters on the seven encoders, one value large, three small, a
 * decorative waveform. Big type, few things on screen, one accent colour.
 *
 * Controls (matrix ids from felucca-bsp fm1_input.h; the printed labels as Felucca measured them):
 *   K1..K4, SELECT, ALGORITHM, PRESETS   the page's seven parameters, in that order
 *   MASTER pot                            output level
 *   HOME   MAIN (again: CROSS -> SHAPE -> LEVELS -> MAIN)     ENV   envelope        EDIT  FILTER
 *   LFO    LFO 1 -> LFO 2 -> RANDOM                 FX    DRIVE -> DELAY 1 -> DELAY 2 -> DELAYS    SAVE  PRESETS
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
/* One topic a page, four parameters on K1..K4. HOME steps through the pages, SELECT turns through them; the left
 * knobs keep one job each everywhere: SELECT the page, ALGORITHM the filter, PRESETS (Milestone 5) the presets */
enum { PG_OSC1, PG_OSC2, PG_OSC3, PG_NOISE, PG_CROSS, PG_FILTER, PG_ENV, PG_DRIVE, PG_DLY1, PG_DLY2, PG_DLYS,
       PG_LFO1, PG_LFO2, PG_RAND, PG_PRESET, PG_COUNT };
#define PG_CYCLE PG_PRESET                /* HOME and SELECT go round the pages before this one */
typedef struct {
    const char *tag;                      /* the page's name, top right */
    int8_t slot[4];                       /* K1 K2 K3 K4 -> parameter, -1 none */
    const char *later;                    /* not built yet: what the page says */
} page_t;
#define NONE4 {-1, -1, -1, -1}
static const page_t PAGES[PG_COUNT] = {
    [PG_OSC1] = {"OSC 1", {P_OSC1, P_WAVE1, P_LVL1, P_SUB}, 0},
    [PG_OSC2] = {"OSC 2", {P_OSC2, P_WAVE2, P_LVL2, P_DETUNE}, 0},
    [PG_OSC3] = {"OSC 3", {P_OSC3, P_WAVE3, P_LVL3, P_PW}, 0},
    [PG_NOISE] = {"NOISE", {P_NOISE, P_NTYPE, P_CHAOS, -1}, 0},
    [PG_CROSS] = {"CROSS", {P_FM, P_XMOD, P_RING, P_SYNC}, 0},
    [PG_FILTER] = {"FILTER", {P_CUTOFF, P_RES, P_ENVF, P_FTYPE}, 0},
    [PG_ENV] = {"ENVELOPE", {P_ATK, P_DEC, P_SUS, P_REL}, 0},
    [PG_DRIVE] = {"DRIVE", {P_DTYPE, P_DRIVE, P_DTONE, P_DMIX}, 0},
    [PG_DLY1] = {"DELAY 1", {P_T1, P_FB1, P_MOD1, P_PAN1}, 0},
    [PG_DLY2] = {"DELAY 2", {P_T2, P_FB2, P_MOD2, P_PAN2}, 0},
    [PG_DLYS] = {"DELAYS", {P_WET, P_XFB, P_DLTONE, P_DMODE}, 0},
    [PG_LFO1] = {"LFO 1", {P_L1RATE, P_L1DEPTH, P_L1WAVE, P_L1DEST}, 0},
    [PG_LFO2] = {"LFO 2", {P_L2RATE, P_L2DEPTH, P_L2WAVE, P_L2DEST}, 0},
    [PG_RAND] = {"RANDOM", {P_RRATE, P_RDEPTH, P_RSMOOTH, P_RDEST}, 0},
    [PG_PRESET] = {"PRESETS", NONE4, 0},   /* (its own drawing: the sound's name, SAVE TO) */
};

#define FOCUS_PRESET 100                  /* the big value shows the sound's name (PRESETS was turned) */
/* presets.c */
#define PR_USER 10u
static uint32_t pr_count(void);
static uint32_t pr_entry(uint32_t i);
static void pr_name(uint32_t e, char *b);
static void pr_apply(uint32_t e);
static int pr_save(uint32_t k, const int16_t *p);
static uint16_t pr_used(void);

/* matrix button ids (Felucca panel.c PANEL_DEFAULT, measured on hardware) */
enum { BT_OCTDN = 0, BT_OCTUP = 1, BT_FX = 2, BT_SCL = 3, BT_ENV = 4, BT_LFO = 5, BT_EDIT = 6, BT_GLO = 7,
       BT_HOME = 8, BT_SAVE = 9, BT_ARP = 10, BT_SEQ = 11, BT_PLAY = 12, BT_REC = 13 };
/* matrix encoders: SELECT 0, ALGORITHM 1, K1..K4 2..5, PRESETS 6 */
enum { EN_SELECT = 0, EN_ALGO = 1, EN_K1 = 2, EN_PRESETS = 6 };

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
    char sig[3][48];                      /* what each band last showed */
    uint32_t frames;
    uint8_t live;                         /* the main loop runs: the keys are read */
    uint32_t pr_cur;                      /* the sound loaded: an index in the PRESETS knob's list */
    uint8_t pr_edited;                    /* a knob moved since it loaded */
    uint8_t pr_slot;                      /* PRESETS page: the user slot SAVE stores in */
    uint32_t msg_ms;                      /* "SAVED": until when */
    int8_t msg_ok;
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
    ui.pr_edited = 1;
}

static void page_go(uint32_t pg)
{
    ui.page = (uint8_t)pg;
}

static void button(uint32_t b)
{
    switch (b) {
    case BT_HOME: page_go(ui.page + 1u >= PG_CYCLE ? 0u : ui.page + 1u); break;   /* the next page, round */
    case BT_ENV: page_go(PG_ENV); break;
    case BT_EDIT: page_go(PG_FILTER); break;
    case BT_LFO: page_go(ui.page == PG_LFO1 ? PG_LFO2 : ui.page == PG_LFO2 ? PG_RAND : PG_LFO1); break;
    case BT_FX:                                    /* DRIVE -> DELAY 1 -> DELAY 2 -> DELAYS */
        page_go(ui.page >= PG_DRIVE && ui.page < PG_DLYS ? ui.page + 1u : PG_DRIVE);
        break;
    case BT_SAVE:                                  /* PRESETS; on it, SAVE stores the sound in the slot chosen */
        if (ui.page != PG_PRESET) {
            page_go(PG_PRESET);
            break;
        }
        ui.msg_ok = (int8_t)(pr_save(ui.pr_slot, ab.p) == 0);
        ui.msg_ms = fm1_ms + 1500u;
        if (ui.msg_ok) {                           /* the sound is now that user slot */
            uint32_t i;
            for (i = 0; i < pr_count(); i++)
                if (pr_entry(i) == 100u + ui.pr_slot)
                    ui.pr_cur = i;
            ui.pr_edited = 0;
        }
        break;
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
        if (i == EN_SELECT) {                      /* SELECT: the pages, one a click */
            int32_t pg = ui.page >= PG_CYCLE ? (s > 0 ? -1 : PG_CYCLE) : (int32_t)ui.page;
            pg += s > 0 ? 1 : -1;
            page_go((uint32_t)(pg < 0 ? 0 : pg >= PG_CYCLE ? PG_CYCLE - 1 : pg));
            continue;
        }
        if (i == EN_PRESETS) {                     /* PRESETS: the sounds, loaded as they come */
            int32_t c = (int32_t)ui.pr_cur + (s > 0 ? 1 : -1), n = (int32_t)pr_count();
            c = c < 0 ? 0 : c >= n ? n - 1 : c;
            if ((uint32_t)c != ui.pr_cur || ui.pr_edited) {
                ui.pr_cur = (uint32_t)c;
                pr_apply(pr_entry(ui.pr_cur));
                ui.pr_edited = 0;
            }
            ui.focus[ui.page] = FOCUS_PRESET;
            continue;
        }
        if (ui.page == PG_PRESET && i == EN_K1) {  /* PRESETS page, K1: the slot SAVE stores in */
            int32_t k = (int32_t)ui.pr_slot + (s > 0 ? 1 : -1);
            ui.pr_slot = (uint8_t)(k < 0 ? 0 : k >= (int32_t)PR_USER ? PR_USER - 1u : (uint32_t)k);
            continue;
        }
        pi = i == EN_ALGO ? P_CUTOFF : PAGES[ui.page].slot[i - EN_K1];   /* ALGORITHM: the filter, on every page */
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
static void gfx_poll(void)                     /* (not before the main loop: the splash is sent before the scan runs) */
{
    if (ui.live)
        ui_poll();
}

static void ui_leds(void)
{
    static const uint8_t PAGE_BTN[PG_COUNT] = {BT_HOME, BT_HOME, BT_HOME, BT_HOME, BT_HOME, BT_EDIT, BT_ENV,
                                               BT_FX, BT_FX, BT_FX, BT_FX, BT_LFO, BT_LFO, BT_LFO, BT_SAVE};
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
    case FMT_NOISE: put_s(num, brief ? AB_NOISE_BRIEF[v] : AB_NOISE_NAME[v]); break;
    case FMT_SYNC: put_s(num, brief ? AB_SYNC_BRIEF[v] : AB_SYNC_NAME[v]); break;
    case FMT_LWAVE: put_s(num, brief ? AB_LWAVE_BRIEF[v] : AB_LWAVE_NAME[v]); break;
    case FMT_DEST: put_s(num, brief ? AB_DEST_BRIEF[v] : AB_DEST_NAME[v]); break;
    case FMT_FTYPE: put_s(num, brief ? AB_FTYPE_BRIEF[v] : AB_FTYPE_NAME[v]); break;
    case FMT_DTYPE: put_s(num, brief ? AB_DTYPE_BRIEF[v] : AB_DTYPE_NAME[v]); break;
    case FMT_DMODE: put_s(num, AB_DMODE_NAME[v]); break;
    case FMT_PAN:
        if (!v) { put_s(num, "CENTER"); break; }
        put_u(put_s(num, v < 0 ? "L " : "R "), (uint32_t)(v < 0 ? -v : v));
        break;
    case FMT_DTIME: {
        uint32_t m10 = AB_DTIME_MS10[v];
        if (m10 >= 10000u) { put_fix(num, m10, 10000u, 2u); put_s(unit, brief ? "s" : " s"); }
        else { put_u(num, (m10 + 5u) / 10u); put_s(unit, brief ? "ms" : " ms"); }
        break;
    }
    case FMT_LFOHZ: {
        uint32_t h = AB_LFO_HZ100[v];
        if (h < 100u && !brief) put_fix(num, h, 100u, 2u);
        else if (h < 1000u) put_fix(num, h < 5u ? 5u : h, 100u, 1u);   /* (brief: "0.4", never "0.0") */
        else put_u(num, (h + 50u) / 100u);
        put_s(unit, brief ? "Hz" : " Hz");
        break;
    }
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
/* Normal: HEAD (title, page) | VALUE (the last turned, large) | LOWER (the page's four knobs, two by two, the waveform dim
 * behind them). Performance: HEAD | VALUE (huge) | WAVE. A band is redrawn and sent when what it shows changes;
 * LOWER and WAVE also on a timer, for the waveform */
enum { B_HEAD, B_VALUE, B_LOWER, NBAND };
static const uint8_t BAND[NBAND][2] = {{0, 34}, {34, 98}, {98, 240}};
static const uint8_t BAND_P[NBAND][2] = {{0, 40}, {40, 150}, {150, 240}};
#define C_WAVE_BG RGB(0x3C, 0x34, 0x5A)           /* the waveform behind the values: the accent, dimmed */
#define WAVE_MS 90u                               /* the background waveform's refresh (its band is ~45 ms to send) */

static int32_t focus_of(uint32_t pg)
{
    int32_t f = ui.focus[pg];
    return f >= 0 ? f : PAGES[pg].slot[0];
}

static void draw_head(uint32_t y)
{
    const char *tag = PAGES[ui.page].tag;
    gfx_text(MX, (int32_t)y + 6, &F_TITLE, "ABYSS", C_INK);
    int32_t x = 240 - MX - gfx_text_w(&F_LABEL, tag);
    gfx_text(x, (int32_t)y + 11, &F_LABEL, tag, C_INK);
    if (ui.page < PG_CYCLE) {                      /* where it is in the round: "3/10", dim, before the name */
        char n[8], *q = put_u(n, ui.page + 1u);
        *q++ = '/';
        put_u(q, PG_CYCLE);
        gfx_text(x - 8 - gfx_text_w(&F_LABEL, n), (int32_t)y + 11, &F_LABEL, n, C_DIM);
    }
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
    if (pi == FOCUS_PRESET || ui.page == PG_PRESET) {   /* the sound: "PRESET 3/12", its name */
        char lab[24], name[24], *q;
        q = put_u(put_s(lab, "PRESET "), ui.pr_cur + 1u);
        *q++ = '/';
        q = put_u(q, pr_count());
        if (ui.pr_edited)
            put_s(q, "  EDITED");
        pr_name(pr_entry(ui.pr_cur), name);
        gfx_text(MX, (int32_t)y0 + 6, &F_LABEL, lab, ui.pr_edited ? C_ACC : C_DIM);
        gfx_text(MX - 1, (int32_t)y0 + 28, &F_MID, name, C_INK);   /* one size for every name */
        return;
    }
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
            const ab_font_t *f = gfx_text_w(&F_BIG, num) + gfx_text_w(&F_BIG, unit) <= 240 - 2 * MX ? &F_BIG : &F_MID;
            x = gfx_text(MX - 1, (int32_t)y0 + 34, f, num, C_INK);
            gfx_text(x, (int32_t)y0 + 34, f, unit, C_INK);
        }
        return;
    }
    {                                              /* a long name ("BAND PASS") in the smaller face, lower */
        int32_t fits = gfx_text_w(&F_BIG, num) + gfx_text_w(&F_BIG, unit) <= 240 - 2 * MX;
        const ab_font_t *f = fits ? &F_BIG : &F_MID;
        int32_t y = (int32_t)y0 + (fits ? 18 : 28);
        int32_t x = gfx_text(MX - 1, y, f, num, C_INK);
        gfx_text(x, y, f, unit, C_INK);
    }
}

/* the waveform: the output from its last rising zero crossing, a point every 6 px */
static void draw_wave(int32_t mid, int32_t amp, int32_t stroke, uint16_t c)
{
    enum { NPT = 36, STEP = 5 };
    int32_t pts[NPT + 1], i, peak = 0, g;
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
        int32_t ya = mid - ab_clamp((pts[i] * ui.wave_gain) >> 15, -amp, amp);
        int32_t yb = mid - ab_clamp((pts[i + 1] * ui.wave_gain) >> 15, -amp, amp);
        gfx_line(xa, ya, xb > 240 - MX ? 240 - MX : xb, yb, stroke, c);
    }
}

/* one knob's label and value; the one turned last has its label in the accent */
static void draw_cell(int32_t x, int32_t y, int32_t pi, int32_t focus)
{
    char num[16], unit[8];
    value_text(pi, num, unit, 0);                  /* the full form when it fits the column, else the brief one */
    if (gfx_text_w(&F_MID, num) + gfx_text_w(&F_LABEL, unit) > 100)
        value_text(pi, num, unit, 1);
    gfx_text(x, y, &F_LABEL, AB_P[pi].brief, pi == focus ? C_ACC : C_DIM);
    x = gfx_text(x - 1, y + 14, &F_MID, num, C_INK);
    gfx_text(x + 2, y + 14 + F_MID.asc - F_LABEL.asc - 3, &F_LABEL, unit[0] == ' ' ? unit + 1 : unit, C_DIM);
}

/* the page's four knobs, two by two: K1 K2 above, K3 K4 below; the waveform dim beneath */
static void draw_lower(uint32_t y0)
{
    const page_t *pg = &PAGES[ui.page];
    int32_t f = focus_of(ui.page), i, y = (int32_t)y0;
    draw_wave(y + 122, 15, 2, C_WAVE_BG);          /* decoration, behind and below */
    if (pg->later)
        return;
    gfx_fill(MX, y + 3, 240 - 2 * MX, 1, C_RULE);
    if (ui.page == PG_PRESET) {                    /* SAVE TO: a user slot, K1 chooses; SAVE stores */
        char t[24];
        int32_t x;
        gfx_text(MX, y + 12, &F_LABEL, "SAVE TO", C_ACC);
        put_u(put_s(t, "USER "), ui.pr_slot + 1u);
        x = gfx_text(MX - 1, y + 26, &F_MID, t, C_INK);
        gfx_text(x + 8, y + 26 + F_MID.asc - F_LABEL.asc - 3, &F_LABEL,
                 (pr_used() >> ui.pr_slot) & 1u ? "REPLACES" : "EMPTY", C_DIM);
        if (fm1_ms < ui.msg_ms)
            gfx_text(MX, y + 70, &F_MID, ui.msg_ok ? "SAVED" : "NOT SAVED", ui.msg_ok ? C_ACC : C_INK);
        else
            draw_lines(MX, y + 64, "K1 CHOOSES THE SLOT\nPRESS SAVE TO STORE", &F_LABEL, C_DIM, 17);
        return;
    }
    for (i = 0; i < 4; i++)
        if (pg->slot[i] >= 0)
            draw_cell(MX + (i & 1) * 108, y + 12 + (i >> 1) * 50, pg->slot[i], f);
}

/* a band's signature: what it shows; redrawn and sent only when that changes */
static void band_sig(uint32_t b, char *s)
{
    char *p = s;
    const page_t *pg = &PAGES[ui.page];
    int32_t f = focus_of(ui.page), i;
    *p++ = (char)('A' + ui.page);
    *p++ = (char)('0' + ui.perf);
    if (b == B_HEAD) {
        *p++ = (char)('0' + ab.drone);
    } else if (b == B_VALUE && (f == FOCUS_PRESET || ui.page == PG_PRESET)) {
        *p++ = 'P';
        *p++ = (char)('a' + ui.pr_cur);
        *p++ = (char)('0' + ui.pr_edited);
    } else if (b == B_LOWER && ui.page == PG_PRESET) {
        *p++ = (char)('a' + ui.pr_slot);
        *p++ = (char)('0' + (fm1_ms < ui.msg_ms) + 2 * ui.msg_ok);
        *p++ = (char)('a' + (pr_used() & 63u));
        *p++ = (char)('a' + (pr_used() >> 6));
    } else if (b == B_VALUE && !pg->later) {
        char num[16], unit[8];
        *p++ = (char)('a' + f);
        value_text(f, num, unit, 0);
        p = put_s(p, num);
        p = put_s(p, unit);
    } else if (b == B_LOWER && !pg->later && !ui.perf) {
        char num[16], unit[8];
        *p++ = (char)('a' + f);
        for (i = 0; i < 4; i++)
            if (pg->slot[i] >= 0) {
                value_text(pg->slot[i], num, unit, 0);
                p = put_s(p, num);
                *p++ = '|';
            }
    }
    *p = 0;
}

static void ui_frame(void)
{
    static uint32_t wave_ms;
    uint32_t b, now = fm1_ms, wave_due = now - wave_ms >= (ui.perf ? 33u : WAVE_MS);
    const uint8_t (*bands)[2] = ui.perf ? BAND_P : BAND;
    char sig[48];
    ui_leds();
    for (b = 0; b < NBAND; b++) {
        uint32_t y0 = bands[b][0], y1 = bands[b][1], i, same;
        band_sig(b, sig);
        same = !ui.full && !(b == B_LOWER && wave_due);
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
        case B_LOWER:
            wave_ms = now;
            if (ui.perf)
                draw_wave((int32_t)(y0 + y1) / 2, (int32_t)(y1 - y0) / 2 - 10, 3, C_ACC);
            else
                draw_lower(y0);
            break;
        }
        gfx_flush_rows(y0, y1);
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
