/* ABYSS presets: 10 factory sounds (dsp/abyss_presets.h) and 10 user slots in flash.
 *
 * Flash: two 4 KiB sectors in felucca-bsp's FL_XDATA1 (a firmware's own data, which Felucca leaves alone above
 * 0xE8000), written in turn. A record: magic, sequence number, the parameter count it was written with, which slots
 * are used, the values, a checksum. At boot the valid record with the higher sequence wins; a save writes the
 * other sector, so a power cut during a save loses that save only. Bytes 0xF00.. of each sector stay erased (the
 * update loader looks for its record there).
 * Parameters are stored by index and only ever appended (abyss_params.h), so an older record loads with the new
 * parameters at their defaults. */
#define PR_SECT_A 0x000F0000u
#define PR_SECT_B 0x000F1000u
#define PR_MAGIC 0x31505941u                   /* "AYP1" */

typedef struct {
    uint32_t magic, seq;
    uint16_t np, used;                         /* parameters per slot; bit k: user slot k holds a sound */
    int16_t v[PR_USER][P_COUNT];
    uint32_t sum;
} pr_rec_t;

static pr_rec_t pr __attribute__((aligned(4)));   /* the user slots, as in flash */
static uint32_t pr_sector;                     /* where the current record is (0: none yet) */

static uint32_t pr_sum(const pr_rec_t *r, uint32_t bytes)   /* over everything before .sum */
{
    const uint8_t *b = (const uint8_t *)r;
    uint32_t s = 0x9E3779B9u, i;
    for (i = 0; i < bytes; i++)
        s = (s ^ b[i]) * 16777619u;
    return s;
}

static int pr_read(uint32_t off, void *dst, uint32_t n)
{
    uint8_t *d = dst;
    while (n) {                                /* 256 bytes at a time with interrupts off: the audio keeps up */
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

/* a record at off, of any parameter count it was written with: 1 = loaded into pr */
static int pr_load_from(uint32_t off, uint32_t *seq)
{
    static uint8_t buf[0xF00] __attribute__((aligned(4)));
    const pr_rec_t *h = (const pr_rec_t *)(void *)buf;
    uint32_t np, bytes, i, k, sum;
    if (pr_read(off, buf, sizeof buf) || h->magic != PR_MAGIC)
        return 0;
    np = h->np;
    bytes = 12u + PR_USER * np * 2u;           /* magic seq np used, the values */
    if (!np || bytes + 4u > sizeof buf)
        return 0;
    sum = (uint32_t)buf[bytes] | (uint32_t)buf[bytes + 1u] << 8 | (uint32_t)buf[bytes + 2u] << 16 |
          (uint32_t)buf[bytes + 3u] << 24;
    if (sum != pr_sum((const pr_rec_t *)(void *)buf, bytes))
        return 0;
    *seq = h->seq;
    pr.used = h->used;
    for (k = 0; k < PR_USER; k++) {
        const uint8_t *v = buf + 12u + k * np * 2u;
        for (i = 0; i < P_COUNT; i++) {
            int32_t x = i < np ? (int16_t)(v[2u * i] | v[2u * i + 1u] << 8) : AB_P[i].def;
            pr.v[k][i] = (int16_t)(x < AB_P[i].min ? AB_P[i].min : x > AB_P[i].max ? AB_P[i].max : x);
        }
    }
    return 1;
}

static void pr_boot(void)
{
    uint32_t sa = 0, sb = 0, a, b;
    pr.magic = PR_MAGIC;
    pr.np = P_COUNT;
    if (!flash_ok)
        return;
    a = pr_load_from(PR_SECT_A, &sa);
    b = pr_load_from(PR_SECT_B, &sb);
    if (a && b) {                              /* both good: the newer one */
        if ((int32_t)(sb - sa) > 0)
            b = pr_load_from(PR_SECT_B, &sb), a = 0;
        else
            a = pr_load_from(PR_SECT_A, &sa), b = 0;
    }
    pr.seq = a ? sa : b ? sb : 0u;
    pr_sector = a ? PR_SECT_A : b ? PR_SECT_B : 0u;
}

static void audio_silence(void);

/* store the current sound in user slot k: 0 = done */
static int pr_save(uint32_t k, const int16_t *p)
{
    uint32_t i, took, f, off = pr_sector == PR_SECT_A ? PR_SECT_B : PR_SECT_A;
    int rc;
    if (!flash_ok || k >= PR_USER)
        return -1;
    for (i = 0; i < P_COUNT; i++)
        pr.v[k][i] = p[i];
    pr.used |= (uint16_t)(1u << k);
    pr.seq++;
    pr.np = P_COUNT;
    pr.sum = pr_sum(&pr, (uint32_t)((uint8_t *)&pr.sum - (uint8_t *)&pr));
    f = irq_save();                            /* the erase stops everything for ~20 ms: silence, not a buzz */
    audio_silence();
    rc = FL_FAR(fl_erase4k_ram)(off, &took);
    irq_restore(f);
    if (!rc)
        rc = fl_write(off, (const uint8_t *)&pr, sizeof pr);
    if (!rc)
        pr_sector = off;
    return rc;
}

/* ---- the list the PRESETS knob turns through: factory 1..10, then the user slots that hold a sound ---- */
static uint32_t pr_count(void)
{
    uint32_t n = AB_NFACTORY, k;
    for (k = 0; k < PR_USER; k++)
        n += (pr.used >> k) & 1u;
    return n;
}
/* entry i of the list -> factory preset (0..9) or user slot (100 + k) */
static uint32_t pr_entry(uint32_t i)
{
    uint32_t k;
    if (i < AB_NFACTORY)
        return i;
    i -= AB_NFACTORY;
    for (k = 0; k < PR_USER; k++)
        if ((pr.used >> k) & 1u) {
            if (!i)
                return 100u + k;
            i--;
        }
    return 0;
}
static void pr_name(uint32_t e, char *b)        /* "INDUSTRIAL DRONE", "USER 3" */
{
    if (e < 100u)
        put_s(b, AB_FACTORY[e].name);
    else
        put_u(put_s(b, "USER "), e - 100u + 1u);
}
static void pr_apply(uint32_t e)
{
    int16_t v[P_COUNT];
    uint32_t i;
    if (e < 100u)
        ab_preset_values(e, v);
    else
        for (i = 0; i < P_COUNT; i++)
            v[i] = pr.v[e - 100u][i];
    for (i = 0; i < P_COUNT; i++)
        if (!ab_is_setting(i))                 /* (MIDI CH, BEND stay as set) */
            ab.p[i] = v[i];
}

/* ---- settings kept over a power-off: the sound last chosen (a factory or user preset, not its edits), MIDI
 * CHANNEL and BEND. Records of 8 bytes appended to one sector (0xF3000), the last valid one wins; the sector is
 * erased only when full (or when it holds someone else's data), so the audio stops for an erase only rarely.
 * Written 3 s after the last change. */
#define SET_SECT 0x000F3000u
#define SET_END 0xE00u
static uint32_t set_at;                        /* where the next record goes; SET_END + 1: erase first */
static uint8_t set_dirty;
static uint32_t set_due;

static uint32_t set_boot(void)                 /* the entry to start with (0: factory 1); MIDI CH, BEND set */
{
    static uint8_t buf[256];
    uint32_t o, i, f, entry = 0;
    set_at = SET_END + 1u;
    if (!flash_ok)
        return 0;
    for (o = 0; o < SET_END; o += 256u) {
        f = irq_save();
        FL_FAR(fl_read_ram)(SET_SECT + o, buf, 256);
        irq_restore(f);
        for (i = 0; i < 256u; i += 8u) {
            const uint8_t *r = buf + i;
            if (r[0] == 0xFFu) {               /* the end of the records */
                set_at = o + i;
                return entry;
            }
            if (r[0] != 0x5Au || (r[1] ^ r[2] ^ r[3] ^ 0x5Au) != r[4])
                return entry;                  /* not ours: erase before the first record */
            entry = r[1];
            if (r[2] <= 16u)
                ab.p[P_MIDICH] = r[2];
            if (r[3] <= 24u)
                ab.p[P_BEND] = r[3];
        }
    }
    return entry;                              /* full: erase before the next */
}

static void set_mark(void)
{
    set_dirty = 1;
    set_due = fm1_ms + 3000u;
}

static void set_service(uint32_t entry)        /* main loop */
{
    uint8_t r[8];
    uint32_t took, f;
    if (!set_dirty || (int32_t)(fm1_ms - set_due) < 0 || !flash_ok)
        return;
    set_dirty = 0;
    if (set_at + 8u > SET_END) {               /* full, or not ours: start the sector again */
        f = irq_save();
        audio_silence();
        FL_FAR(fl_erase4k_ram)(SET_SECT, &took);
        irq_restore(f);
        set_at = 0;
    }
    r[0] = 0x5Au;
    r[1] = (uint8_t)entry;
    r[2] = (uint8_t)ab.p[P_MIDICH];
    r[3] = (uint8_t)ab.p[P_BEND];
    r[4] = (uint8_t)(r[1] ^ r[2] ^ r[3] ^ 0x5Au);
    r[5] = r[6] = r[7] = 0xFFu;
    if (!fl_write(SET_SECT + set_at, r, 8))
        set_at += 8u;
}
static uint16_t pr_used(void) { return pr.used; }
