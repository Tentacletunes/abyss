/* ABYSS graphics: one 240x240 RGB565 frame in RAM (pixels byte-swapped, as the panel takes them), drawn into
 * by the UI, sent to the ST7789 by rows that changed. The panel's SPI runs at 12 MHz: a full frame is ~77 ms, so
 * the UI only ever sends the bands it redrew, a chunk at a time, polling the keys between chunks. */
#define W 240u
#define H 240u
#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | ((b) >> 3)))
#define SWAP16(c) ((uint16_t)(((c) >> 8) | ((c) << 8)))

static uint16_t fb[W * H] __attribute__((section(".pool"), aligned(4)));
static uint8_t lcd_small[8];
static uint8_t lcd_busy;

static void gfx_poll(void);      /* the UI: keys and MIDI while a band goes out */

static void lcd_sync(void)
{
    if (!lcd_busy)
        return;
    fm1_lcd_wait();
    fm1_lcd_deselect();
    lcd_busy = 0;
}
static void lcd_cmd(uint8_t c)
{
    lcd_sync();
    fm1_lcd_send_cmd(c);
    fm1_lcd_wait();
    fm1_lcd_deselect();
}
static void lcd_data(const void *p, uint32_t n)
{
    if (!n)
        return;
    lcd_sync();
    fm1_lcd_send_data(p, n);
    lcd_busy = 1;
}
static void lcd_window(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
{
    lcd_small[0] = 0; lcd_small[1] = (uint8_t)x0; lcd_small[2] = 0; lcd_small[3] = (uint8_t)x1;
    lcd_cmd(0x2A);
    lcd_data(lcd_small, 4);
    lcd_sync();
    lcd_small[4] = 0; lcd_small[5] = (uint8_t)y0; lcd_small[6] = 0; lcd_small[7] = (uint8_t)y1;
    lcd_cmd(0x2B);
    lcd_data(lcd_small + 4, 4);
    lcd_sync();
    lcd_cmd(0x2C);
}
static void lcd_spin(uint32_t n)
{
    for (volatile uint32_t i = 0; i < n; i++)
        ;
}
static void lcd_init(void)
{
    static const uint8_t seq[] = {0x01, 0, 0x00, 150, 0x11, 0, 0x00, 120, 0x3A, 1, 0x55, 0x36, 1, 0x00,
                                  0x21, 0, 0x13, 0};
    uint32_t r = 0;
    fm1_lcd_hw_init();
    lcd_spin(2000000u);
    while (r < sizeof seq) {
        if (seq[r] == 0x00) {                    /* wait ~ms */
            lcd_spin(seq[r + 1u] * 25000u);
            r += 2u;
            continue;
        }
        lcd_cmd(seq[r]);
        if (seq[r + 1u]) {
            lcd_small[0] = seq[r + 2u];
            lcd_data(lcd_small, 1);
            lcd_sync();
        }
        r += 2u + seq[r + 1u];
    }
    fm1_lcd_baud(4);                             /* 12 MHz, as Felucca */
}

/* send rows y0..y1-1 of the frame, 16 rows a transfer, the keys polled while each goes out */
static void gfx_flush_rows(uint32_t y0, uint32_t y1)
{
    if (y1 > H)
        y1 = H;
    if (y0 >= y1)
        return;
    lcd_window(0, y0, W - 1u, y1 - 1u);
    while (y0 < y1) {
        uint32_t n = y1 - y0 > 16u ? 16u : y1 - y0;
        lcd_data(&fb[y0 * W], n * W * 2u);
        gfx_poll();
        lcd_sync();
        y0 += n;
    }
}

/* ---------------------------------------------------------------- drawing --- */
static void gfx_fill(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    int32_t i, j;
    uint16_t s = SWAP16(c);
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int32_t)W) w = (int32_t)W - x;
    if (y + h > (int32_t)H) h = (int32_t)H - y;
    for (j = 0; j < h; j++) {
        uint16_t *p = &fb[(uint32_t)(y + j) * W + (uint32_t)x];
        for (i = 0; i < w; i++)
            p[i] = s;
    }
}

static inline void gfx_blend(uint16_t *p, uint16_t c, uint32_t a)   /* a: 0..15 coverage of colour c */
{
    uint32_t d = SWAP16(*p), k = a * 17u;                            /* 0..255 */
    int32_t dr = (int32_t)(d >> 11), dg = (int32_t)((d >> 5) & 63u), db = (int32_t)(d & 31u);
    int32_t cr = (int32_t)(c >> 11), cg = (int32_t)((c >> 5) & 63u), cb = (int32_t)(c & 31u);
    dr += ((cr - dr) * (int32_t)k + 128) >> 8;
    dg += ((cg - dg) * (int32_t)k + 128) >> 8;
    db += ((cb - db) * (int32_t)k + 128) >> 8;
    *p = SWAP16((uint16_t)(dr << 11 | dg << 5 | db));
}

static const ab_glyph_t *gfx_glyph(const ab_font_t *f, char ch)
{
    uint32_t c = (uint8_t)ch;
    if (c < 32u || c > 126u)
        c = '?';
    return &f->g[c - 32u];
}

static int32_t gfx_text_w(const ab_font_t *f, const char *s)
{
    int32_t w = 0;
    while (*s)
        w += gfx_glyph(f, *s++)->adv;
    return w;
}

/* text with its ascent box's top-left at (x, y); returns the x after it */
static int32_t gfx_text(int32_t x, int32_t y, const ab_font_t *f, const char *s, uint16_t c)
{
    for (; *s; s++) {
        const ab_glyph_t *g = gfx_glyph(f, *s);
        const uint8_t *d = f->d + ((uint32_t)g->hi << 16 | g->off);
        uint32_t bw = (g->w + 1u) >> 1, i, j;
        for (j = 0; j < g->h; j++) {
            int32_t py = y + g->y + (int32_t)j;
            if (py < 0 || py >= (int32_t)H)
                continue;
            for (i = 0; i < g->w; i++) {
                int32_t px = x + g->x + (int32_t)i;
                uint32_t a = d[j * bw + (i >> 1)];
                a = i & 1u ? a & 15u : a >> 4;
                if (a && px >= 0 && px < (int32_t)W)
                    gfx_blend(&fb[(uint32_t)py * W + (uint32_t)px], c, a);
            }
        }
        x += g->adv;
    }
    return x;
}

/* a square brush of side t along a line: the waveform's stroke */
static void gfx_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t t, uint16_t c)
{
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int32_t sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy, e2, h = t / 2;
    for (;;) {
        gfx_fill(x0 - h, y0 - h, t, t, c);
        if (x0 == x1 && y0 == y1)
            break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
