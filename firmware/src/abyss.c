/* ABYSS: an experimental noise synthesizer firmware for the M-VAVE FM-1.
 * One translation unit: felucca-bsp (MPL-2.0) for the hardware, then the engine, the graphics and the UI.
 *
 * Boot: watchdog, boot-loop guard (two boots in a row that never reach the main loop's healthy point at 2 s
 * drop into UBOOT, so a broken build can always be replaced), RAM sections, hardware guards; then the LCD,
 * keys (TIMER5, 10 kHz), audio (ALNK0 half-buffer interrupt) and USB (MIDI, serial console, the update entry). */
#include <stdint.h>
#define FELUCCA_CDC 1                         /* USB serial: a status line (CPU load) every second */
#define FELUCCA_OTA 1                         /* the installer can replace ABYSS over USB-MIDI */
#ifndef ABYSS_BUILD
#define ABYSS_BUILD "dev"
#endif
#ifndef ABYSS_ID
#define ABYSS_ID "FM-1_1300"
#endif
#define FELUCCA_ID ABYSS_ID                   /* the identity the installer sees (== the package's) */
#define FELUCCA_USB_MANUFACTURER "ABYSS"
#define FELUCCA_USB_PRODUCT "ABYSS FM-1"
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#include "fm1_timer.h"
#include "fm1_input.h"
#include "fm1_adc.h"
#include "fm1_lcd_hw.h"
#include "fm1_audio.h"
#include "fm1_flash.h"
#include "libc.c"
#include "usb.c"
#include "ota.c"

#include "abyss_tables.h"
#include "abyss_fonts.h"
#define AB_POOL __attribute__((section(".pool")))   /* the delay lines: the big zeroed RAM */
#include "../dsp/abyss_dsp.c"
#include "../dsp/abyss_presets.h"
#include "gfx.c"
#include "ui.c"

extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _rt_start[], _rt_end[], _rt_load[], _pool_start[], _pool_end[];

/* the boot-loop guard, in RAM that survives a reset: a start that ends in a crash or a watchdog reset before it has
 * run 2 s counts as failed; two in a row and the FM-1 waits in its update mode for an installer. A power-off is not a
 * failure: switched off, the FM-1 can restart a few times as its supply falls (the RAM kept), and those starts
 * must not count, or the next power-on finds the count at two (found on hardware, 2026-11-19) */
static struct { uint32_t magic, pending, failed, crashed; } guard __attribute__((section(".noinit")));
#define GUARD_MAGIC 0x32594241u                 /* "ABY2" */
static uint32_t boots_ok;
static uint8_t blog_main;

/* ---------------------------------------------------------------- audio --- */
#define HALF_FRAMES 128u                       /* 2.9 ms a half buffer */
#define HALF_WORDS (HALF_FRAMES * 2u)
#define OUT_SHIFT 7                            /* Q15 -> the codec's 24-bit word, -6 dBFS ceiling (as Felucca) */
static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(4)));
static volatile uint8_t in_audio;
static volatile uint32_t audio_halves, audio_late, audio_us_max, audio_cpu_q8;
static volatile uint32_t t5_nested;

void fm1_alnk0_irq(void)
{
    uint8_t p;
    uint32_t t0;
    in_audio = 1;
    p = fm1_audio_pending();
    t0 = fm1_ticks();
    t5_nested = 0;
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        uint32_t half = fm1_audio_free_half(), b, i, us;
        int32_t *o = &abuf[half * HALF_WORDS];
        for (b = 0; b < HALF_FRAMES; b += AB_BLOCK) {
            int32_t *q = o + 2u * b;
            dsp_render(q, AB_BLOCK);
            for (i = 0; i < 2u * AB_BLOCK; i++)
                q[i] *= 1 << OUT_SHIFT;
        }
        fm1_audio_ack_half();
        audio_halves++;
        us = (fm1_ticks() - t0 - t5_nested) / FM1_TICKS_PER_US;
        if (us > audio_us_max)
            audio_us_max = us;
        audio_cpu_q8 = (audio_cpu_q8 * 31u + us * 256u * 100u / (HALF_FRAMES * 1000000u / AB_FS)) / 32u;
        if (fm1_audio_free_half() != half)
            audio_late++;                      /* the DMA moved on while we rendered: an underrun */
    }
    in_audio = 0;
}
extern void isr_alnk0(void);

static void audio_init(void)
{
    uint32_t i;
    for (i = 0; i < 2u * HALF_WORDS; i++)
        abuf[i] = 0;
    fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);
}

/* ---------------------------------------------------------------- 10 kHz tick --- */
void fm1_timer5_irq(void)                      /* prio 4, above the audio (3): the key scan never waits */
{
    static uint32_t sub, last, acc, owed;
    uint32_t t0 = fm1_ticks();
    fm1_timer5_ack();
    fm1_input_tick();
    acc += t0 - last;
    last = t0;
    while (acc >= 1000u * FM1_TICKS_PER_US) {
        acc -= 1000u * FM1_TICKS_PER_US;
        fm1_ms++;
    }
    if (++sub == 5u) {
        sub = 0;
        owed = 1;                              /* 2 kHz: USB */
    }
    if (in_audio) {                            /* nested in the render: USB waits for the next tick */
        t5_nested += fm1_ticks() - t0;
        return;
    }
    if (owed) {
        owed = 0;
        usb_poll();
    }
}
extern void isr_timer5(void);

/* ---------------------------------------------------------------- updates (ota.c) --- */
static uint8_t flash_ok;
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { fm1_wdt_feed(); }
static int ota_in_area(uint32_t off, uint32_t n) { return FL_IN(off, n, OTA_AREA, OTA_AREA + OTA_AREA_LEN); }
static int ota_erase(uint32_t off)
{
    uint32_t took;
    if (!ota_in_area(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    return fl_erase4k(off, &took);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!ota_in_area(off, n))
        return -8;
    return fl_write(off, p, n);
}
static int ota_fread(uint32_t off, void *p, uint32_t n)
{
    uint8_t *d = p;
    while (n) {
        uint32_t k = n > 256u ? 256u : n, f = irq_save();
        int rc = FL_FAR(fl_read_ram)(off, d, k);
        irq_restore(f);
        if (rc)
            return rc;
        off += k;
        d += k;
        n -= k;
    }
    return 0;
}
static void ota_show(uint32_t step, int32_t code)   /* the update's steps as a bar */
{
    gfx_fill(0, 200, 240, 24, C_BG);
    if (step < 9u)
        gfx_fill(0, 200, (int32_t)step * 27, 24, C_ACC);
    else
        gfx_fill(0, 200, 240, 24, code == 1 ? RGB(0x40, 0xD0, 0x80) : RGB(0xE0, 0x40, 0x40));
    gfx_flush_rows(200, 224);
}
static void ota_commit(const uint8_t *parm)
{
    guard.pending = 0;                         /* an intentional reset, not a failed boot */
    guard.failed = 0;
    fm1_audio_stop();
    usb_detach();
    fm1_delay_ms(30);
    fm1_enter_update(parm);
}

static void audio_silence(void)                /* (interrupts off) the DMA would loop stale audio: a buzz */
{
    uint32_t i;
    for (i = 0; i < 2u * HALF_WORDS; i++)
        abuf[i] = 0;
}
#include "presets.c"

/* ---------------------------------------------------------------- boot log (diagnostic) --- */
/* Each start appends a few bytes to one flash sector (0xF2000, felucca-bsp's FL_XDATA1): how far it got. A flash
 * backup (the rescue tool makes one) then shows where a start that never reached the screen stopped.
 *   B0 p3_rst rst_src   the start, after the RAM setup, with the reset cause
 *   B1 supplies raised   B2 guards on   C0..C5 app_main's steps (boot_mark)   C8 main loop   C9 2 s healthy
 *   EE vec pc(4) rets(4) a fault */
#define BLOG_SECT 0x000F2000u
#define BLOG_END 0xE00u                          /* (0xF00.. stays erased: the update loader looks there) */
static uint32_t blog_at;
static uint8_t blog_on;
static uint32_t blog_id;
static void blog(uint8_t b)
{
    uint8_t v = b;
    if (!blog_on || blog_at >= BLOG_END)
        return;
    if (!fl_write(BLOG_SECT + blog_at, &v, 1))
        blog_at++;
}
static void blog32(uint32_t v)
{
    blog((uint8_t)v); blog((uint8_t)(v >> 8)); blog((uint8_t)(v >> 16)); blog((uint8_t)(v >> 24));
}
static void blog_open(uint8_t p3, uint8_t src)
{
    static uint8_t buf[256];
    uint32_t f, id, o, i, took;
    f = irq_save();
    id = FL_FAR(fl_jedec_ram)();
    irq_restore(f);
    blog_id = id;                              /* (logged below; a start-up flash in another mode reads wrong) */
    blog_at = BLOG_END;
    for (o = 0; o < BLOG_END && blog_at == BLOG_END; o += 256u) {   /* the first erased byte */
        f = irq_save();
        FL_FAR(fl_read_ram)(BLOG_SECT + o, buf, 256);
        irq_restore(f);
        for (i = 0; i < 256u; i++)
            if (buf[i] == 0xFFu) {
                blog_at = o + i;
                break;
            }
    }
    if (blog_at > BLOG_END - 64u) {                /* full: start again */
        f = irq_save();
        FL_FAR(fl_erase4k_ram)(BLOG_SECT, &took);
        irq_restore(f);
        blog_at = 0;
    }
    blog_on = 1;
    blog(0xB0);
    blog(p3);
    blog(src);
    blog((uint8_t)blog_id);
    blog((uint8_t)(blog_id >> 8));
    blog((uint8_t)(blog_id >> 16));
}

/* a fault: a red screen with the address, then a reset; the record survives in fm1_crash */
static void fm1_fault(const fm1_crash_t *c)
{
    char b[12];
    uint32_t i;
    fm1_audio_stop();
    blog(0xEE);
    blog((uint8_t)c->vec);
    blog32(c->pc);
    blog32(c->rets);
    gfx_fill(0, 0, 240, 240, RGB(0x90, 0x10, 0x18));
    gfx_text(MX, 30, &F_TITLE, "ABYSS CRASH", C_INK);
    for (i = 0; i < 8u; i++)
        b[i] = "0123456789ABCDEF"[(c->pc >> (28u - 4u * i)) & 15u];
    b[8] = 0;
    gfx_text(MX, 70, &F_MID, b, C_INK);
    lcd_busy = 0;
    gfx_flush_rows(0, 240);
    fm1_delay_ms(3000);
    guard.crashed = 1;                         /* (a failed start, if it came before the 2 s) */
    fm1_reboot();
}

/* ---------------------------------------------------------------- console --- */
static void put(char c)
{
    if (!cdc.dtr || co_w - co_r >= CO_N)
        return;
    cdc_out[co_w % CO_N] = (uint8_t)c;
    RING_PUBLISH();
    co_w++;
}
static void puts_(const char *s) { while (*s) put(*s++); }
static void putd(const char *k, uint32_t v)
{
    char b[12];
    puts_(k);
    put_u(b, v);
    puts_(b);
    put(' ');
}
static void status(void)
{
    putd("ms=", fm1_ms);
    putd("cpu%=", audio_cpu_q8 >> 8);
    putd("max_us=", audio_us_max);
    putd("late=", audio_late);
    putd("note=", ab.note);
    putd("gate=", ab.gate);
    putd("drone=", ab.drone);
    putd("frames=", ui.frames);
    puts_("\r\n");
}

/* ---------------------------------------------------------------- main --- */
static void splash(void)
{
    gfx_fill(0, 0, 240, 240, C_BG);
    gfx_text((240 - gfx_text_w(&F_BIG, "ABYSS")) / 2, 82, &F_BIG, "ABYSS", C_INK);
    gfx_text((240 - gfx_text_w(&F_LABEL, "EXPERIMENTAL NOISE SYNTHESIZER")) / 2, 140, &F_LABEL,
             "EXPERIMENTAL NOISE SYNTHESIZER", C_DIM);
    gfx_fill(104, 168, 32, 3, C_ACC);
    gfx_text((240 - gfx_text_w(&F_LABEL, ABYSS_BUILD)) / 2, 214, &F_LABEL, ABYSS_BUILD, C_RULE);   /* which build */
    gfx_flush_rows(0, 240);
}

/* the start's steps as small dots under the splash: a start that stops shows how far it got (0 screen,
 * 1 flash, 2 sound and presets, 3 keys, 4 audio, 5 USB and interrupts) */
static void boot_mark(uint32_t k)
{
    blog((uint8_t)(0xC0u + k));
    gfx_fill(96 + (int32_t)k * 9, 196, 5, 5, C_DIM);
    gfx_flush_rows(196, 201);
}

/* the boot log's last bytes as hex on the screen (REC held at power-on), until any button */
static void show_boot_log(void)
{
    static uint8_t buf[256];
    uint32_t f, start, i, n, row;
    char line[40];
    start = blog_at > 120u ? blog_at - 120u : 0u;
    f = irq_save();
    FL_FAR(fl_read_ram)(BLOG_SECT + start, buf, 120);
    irq_restore(f);
    n = blog_at - start;
    gfx_fill(0, 0, 240, 240, C_BG);
    gfx_text(MX, 6, &F_LABEL, "BOOT LOG", C_ACC);
    for (row = 0; row * 10u < n; row++) {
        char *q = line;
        for (i = row * 10u; i < n && i < row * 10u + 10u; i++) {
            *q++ = "0123456789ABCDEF"[buf[i] >> 4];
            *q++ = "0123456789ABCDEF"[buf[i] & 15u];
            *q++ = ' ';
        }
        *q = 0;
        gfx_text(MX, 26 + (int32_t)row * 17, &F_LABEL, line, C_INK);
    }
    gfx_flush_rows(0, 240);
    while ((fm1_in.buttons >> BT_REC) & 1u)
        fm1_wdt_feed();
    while (!fm1_in.buttons)
        fm1_wdt_feed();
    ui.full = 1;
}

static void app_main(void)
{
    uint32_t last_status = 0, last_frame = 0, last_pot = 0;
    lcd_init();                                /* the screen first: whatever stops the start shows where */
    splash();
    lcd_cmd(0x29);                             /* DISPON */
    fm1_lcd_backlight(1);
    boot_mark(0);
    {
        uint32_t f = irq_save(), id = FL_FAR(fl_jedec_ram)();
        irq_restore(f);
        flash_ok = id == 0x856014u || id == 0xB36014u;   /* the 1 MiB parts FM-1s have */
        if (flash_ok)
            ota_boot_cleanup();
    }
    boot_mark(1);
    dsp_init();
    ui_init();
    pr_boot();                                 /* the user slots from flash; power-on plays factory preset 1 */
    pr_apply(pr_entry(0));
    ui.focus[PG_OSC1] = FOCUS_PRESET;          /* (the screen opens on the sound's name) */
    boot_mark(2);
    fm1_input_init();
    fm1_adc_init();
    boot_mark(3);
    audio_init();
    boot_mark(4);
    usb_start();
    fm1_timer5_start(isr_timer5, 4);
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    boot_mark(5);
    while (fm1_ms < 900u)                      /* the splash */
        fm1_wdt_feed();
    if ((fm1_in.buttons >> BT_REC) & 1u)       /* REC held at power-on: the boot log, until a key */
        show_boot_log();
    {                                          /* what the scan settled on meanwhile is not a turn, nor a press */
        uint32_t i, rel;
        for (i = 0; i < FM1_NENC; i++)
            (void)fm1_enc_take(i);
        (void)fm1_input_edges(&rel);
        ui.keys = fm1_in.notes;
    }
    ui.full = 1;
    ui.live = 1;
    for (;;) {
        uint32_t now = fm1_ms;
        fm1_wdt_feed();
        usb_retry(now);
        if (usb.uboot_req) {
            fm1_audio_stop();
            usb_detach();
            fm1_enter_uboot();
        }
        ota_service();
        if (usb.ota_req) {
            usb.ota_req = 0;
            if (flash_ok)
                ota_session();
            ui.full = 1;
        }
        if (!blog_main) {
            blog_main = 1;
            blog(0xC8);
        }
        if (!boots_ok && now > 2000u) {
            blog(0xC9);        /* it runs: the next boot is not a loop */
            boots_ok = 1;
            guard.pending = 0;
            guard.failed = 0;
        }
        ui_poll();
        if (now - last_pot >= 20u) {           /* MASTER: an audio taper */
            int32_t v = fm1_adc_read(FM1_ADC_MASTER);
            last_pot = now;
            if (v >= 0)
                ab.master = (int16_t)((v * v) / 32);   /* 1023^2 / 32 = 32704 */
        }
        if (now - last_frame >= 33u) {         /* ~30 frames a second at most */
            last_frame = now;
            ui_frame();
        }
        while (ci_r != ci_w) {                 /* console: 'p' status now, 'u' UBOOT */
            char c = (char)cdc_in[ci_r % CI_N];
            ci_r++;
            if (c == 'u')
                usb.uboot_req = 1;
            else
                status();
        }
        if (now - last_status >= 1000u) {
            last_status = now;
            status();
        }
    }
}

void fm1_cstart(void)
{
    uint32_t *s, *d, p3, src, failed_start;
    fm1_cpu1_stop();                           /* a second core still running from before this reset (the stock
                                                * firmware uses both) would scribble on the RAM cleared below */
    fm1_time_init();
    fm1_reset_reason();
    p3 = fm1_boot.p3_rst;                      /* (fm1_boot is in .bss, cleared below) */
    src = fm1_boot.rst_src;
    fm1_wdt_arm(0x0D);
    if (guard.magic != GUARD_MAGIC) {
        guard.magic = GUARD_MAGIC;
        guard.failed = 0;
        guard.pending = 0;
        guard.crashed = 0;
    }
    /* the last start did not reach 2 s AND it ended in a crash (fm1_fault) or the watchdog (P3_RST_SRC bit 2) */
    failed_start = guard.pending && (guard.crashed || (p3 & 0x04u));
    guard.failed = failed_start ? guard.failed + 1u : 0u;
    guard.crashed = 0;
    guard.pending = 1;
    fm1_irq_init();
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (d = _pool_start; d < _pool_end; d++)
        *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++)
        *d = *s;
    fm1_mailbox_clear();                       /* the boot-state mailbox and the update record (0x01C7FD80..): left
                                                * set, the chip's boot code takes the next restart (even the power
                                                * switch, while USB keeps the RAM alive) into its update mode */
    blog_open((uint8_t)p3, (uint8_t)src);
    blog((uint8_t)guard.failed);
    if (guard.failed >= 2u) {
        blog(0xA9);
        guard.failed = 0;
        guard.pending = 0;
        fm1_enter_uboot();
    }
    blog(0xB1);                                /* (the supplies as the boot loader leaves them, as Felucca: raising
                                                * them, and the DC-DC switch-over, are not needed and a load on a
                                                * battery at power-on) */
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    blog(0xB2);
    app_main();
    for (;;)
        ;
}
