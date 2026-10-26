/*
 * main.c -- standalone test, PID 1, no TWRP.
 *
 * THE SCREEN IS THE ONLY OUTPUT CHANNEL.
 *
 * There is no console, no adb and no log, so the panel has to carry the result.
 * Previous runs painted a solid colour and then hung, which made "stuck on
 * green" ambiguous between at least three very different failures:
 *
 *   - stage 6 never completed (second touch never detected)
 *   - the kernel accepted reboot(POWER_OFF) but halted without cutting power
 *   - reboot() was refused and returned an error we could not read
 *
 * So each stage now has its OWN colour, the touch waits FLASH so a live
 * process is distinguishable from a frozen one, and the colour immediately
 * before the reboot call is different from the colour drawn if reboot returns.
 *
 *   flashing MAGENTA  = alive, waiting for the FIRST touch
 *   flashing GREEN    = alive, waiting for the SECOND touch
 *   solid BLUE        = about to call reboot(POWER_OFF)
 *   solid RED         = reboot() RETURNED -- the call was refused
 *   frozen on BLUE    = the kernel took the call and halted, but did not
 *                       cut the rails (see Entry 44/45)
 *   frozen on a
 *   flashing colour   = the process died mid-wait; the kernel killed us
 *
 * That single change turns an unobservable hang into a diagnosis.
 *
 * WHY THE STAGE COLOURS ARE SAFE
 * The display path is already proven: open fb0 -> mmap -> fill BOTH planes ->
 * MSMFB_DISPLAY_COMMIT. Committing a plane we have not written produced a
 * blank screen once, so both planes are always filled before every commit.
 *
 * WHY NO setprop FOR POWER-OFF
 * There is no TWRP, so there is no init to handle sys.powerctl and no
 * /sbin/setprop. reboot(2) has to work.
 *
 * ARM's signature is reboot(magic1, magic2, cmd, arg) -- the MAGIC WORDS COME
 * FIRST and the command is the THIRD argument (kernel/sys.c:455). Passing cmd
 * first puts 0x4321 where magic1 must be 0xfee1dead and returns -EINVAL.
 *
 * SYSCALL NUMBERS come from asm/unistd-eabi.h. This project once shipped
 * read=45, mknod=33, pwrite64=146, statfs=140 and reboot=142 -- all wrong.
 * reboot=88, and 142 is _newselect, which made reboot "appear to block".
 *
 * WHY DEVICE NODES ARE STATIC
 * This kernel has no devtmpfs (/proc/filesystems lists only tmpfs and ext4),
 * so a minimal ramdisk starts with an empty /dev. mknod(2) returns EINVAL to an
 * unlabelled process, so nodes cannot be created at runtime either. They are
 * baked into the cpio archive as newc device entries instead, which the kernel
 * creates with no syscall from us.
 */
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef int            s32;

/* ---- flags ------------------------------------------------------------ */
#define O_RDONLY   0x0000
#define O_RDWR     0x0002
#define O_NONBLOCK 0x0800
#define O_NOCTTY   0x0400     /* REQUIRED by /dev/bcm2079x; without it write is EIO */

/* ---- syscalls (verified in asm/unistd-eabi.h) ------------------------- */
#define SYS_read    3
#define SYS_write   4
#define SYS_open    5
#define SYS_close   6
#define SYS_ioctl  54
#define SYS_getpid 20
#define SYS_mount  21
#define SYS_umount2 22
#define SYS_mkdir  39
#define SYS_reboot 88          /* NOT 142: that is _newselect */
#define SYS_fsync 118
#define SYS_poll  168
#define SYS_mmap2 192

#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define MAP_SHARED  0x01
#define PAGE        4096

/* ---- framebuffer (sysfs: stride 4352, virtual_size 1080x3840) ---------- */
#define FB_W      1080
#define FB_H      1920
#define FB_STRIDE 4352

/* Left/right margin for anything drawn on screen, in u32 words (1 word = 1
 * pixel here, since the panel is 32bpp). Defined up here because both the
 * pattern geometry and the text renderer need it. */
#define PAT_MARGIN_WORDS 32

/* ---- MSMFB display commit, transcribed from include/linux/fb.h and
 * include/linux/msm_mdp.h.
 *
 * The PREVIOUS transcription of struct fb_var_screeninfo was WRONG: it was 112
 * bytes where the kernel's is 160. It had invented `u16 activate_s,
 * u16 rotate_2, u16 reserved[4]` at the end and, far worse, it omitted all
 * four `struct fb_bitfield` members (red/green/blue/transp, 12 bytes each)
 * that sit immediately after `grayscale`.
 *
 * That made struct mdp_display_commit 120 bytes instead of 168, so a
 * 168-byte ioctl was being handed a 120-byte object -- 48 bytes of the
 * kernel's read would have run past the end of ours. It appeared to work only
 * because every field the commit path reads (xres, yres, xres_virtual,
 * yres_virtual, xoffset, yoffset, bits_per_pixel) sits BEFORE the bitfields,
 * so the divergence was never reached. The unused tail simply read as zeros
 * out of .bss.
 *
 * The tripwires below are why this was caught: they were written expecting the
 * kernel's 160/168 and fired on the first build. Layout, not a magic number.
 */
struct fb_bitfield {
    u32 offset;
    u32 length;
    u32 msb_right;
};

struct fb_var_screeninfo {
    u32 xres, yres;
    u32 xres_virtual, yres_virtual;
    u32 xoffset, yoffset;
    u32 bits_per_pixel;
    u32 grayscale;
    struct fb_bitfield red, green, blue, transp;
    u32 nonstd, activate, height, width, accel_flags;
    u32 pixclock, left_margin, right_margin, upper_margin, lower_margin;
    u32 hsync_len, vsync_len, sync, vmode, rotate, colorspace;
    u32 reserved[4];
};

struct mdp_display_commit {
    u32 flags;
    u32 wait_for_finish;
    struct fb_var_screeninfo var;
};

/* From include/linux/msm_mdp.h:
 *     #define MSMFB_DISPLAY_COMMIT _IOW(MSMFB_IOCTL_MAGIC, 164, struct mdp_display_commit)
 * with MSMFB_IOCTL_MAGIC == 'm'. _IOW encodes the SIZE OF ITS THIRD ARGUMENT,
 * so the ioctl number is only correct while our struct matches the kernel's
 * byte for byte. From the headers:
 *     sizeof(struct fb_bitfield)        = 12
 *     sizeof(struct fb_var_screeninfo)  = 160   (0xA0)
 *     sizeof(struct mdp_display_commit) = 168   (0xA8)
 *     (1<<30) | (168<<16) | ('m'<<8) | 164      = 0x40A86DA4
 *
 * These tripwires turn any future layout drift into a BUILD FAILURE rather
 * than an out-of-bounds kernel read. gcc rejects a negative array bound.
 */
#define MSMFB_DISPLAY_COMMIT 0x40A86DA4u

typedef char tripwire_bitfield_size[(sizeof(struct fb_bitfield) == 12) ? 1 : -1];
typedef char tripwire_fb_var_size[(sizeof(struct fb_var_screeninfo) == 160) ? 1 : -1];
typedef char tripwire_commit_size[(sizeof(struct mdp_display_commit) == 168) ? 1 : -1];
typedef char tripwire_commit_ioctl[
    (MSMFB_DISPLAY_COMMIT ==
     ((1u << 30) | (sizeof(struct mdp_display_commit) << 16) | ('m' << 8) | 164u))
    ? 1 : -1];

/* ---- NFC ioctls, from b/nfc/bcm2079x.h -------------------------------- */
#define BCMNFC_POWER_CTL             0xFA01
#define BCMNFC_WAKE_CTL              0xFA05

/* ---- touch ------------------------------------------------------------ */
#define EV_ABS              0x03
#define ABS_MT_POSITION_X   0x35
#define ABS_MT_POSITION_Y   0x36
#define ABS_MT_TRACKING_ID  0x39

/* ---- reboot --------------------------------------------------------------
 * Constants come from the KERNEL'S OWN HEADER, vendored at
 * include/linux/reboot.h, NOT transcribed here.
 *
 * The previous version had:
 *     #define LINUX_REBOOT_CMD_POWER_OFF 0x4321
 * written from memory. The real value is 0x4321FEDC. 0x4321 matches no case
 * in the switch in kernel/sys.c, so the call fell through to default and
 * returned -EINVAL -- which the diagnostic colour scheme showed as RED.
 *
 * That is the fourth time this project has written a kernel constant from
 * memory instead of reading the header (see verify-constants.sh, which now
 * cross-checks every remaining hand-transcribed value against the tree).
 */
#include <linux/reboot.h>

/* ==========================================================================
 * syscall wrappers
 * ======================================================================== */

static inline long sc(long n, long a, long b, long c, long d, long e)
{
    register long r7 __asm__("r7") = n;
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    register long r2 __asm__("r2") = c;
    register long r3 __asm__("r3") = d;
    register long r4 __asm__("r4") = e;
    __asm__ volatile("svc 0" : "+r"(r0)
                     : "r"(r7), "r"(r1), "r"(r2), "r"(r3), "r"(r4)
                     : "memory");
    return r0;
}

static inline long sc6(long n, long a, long b, long c, long d, long e, long f)
{
    register long r7 __asm__("r7") = n;
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    register long r2 __asm__("r2") = c;
    register long r3 __asm__("r3") = d;
    register long r4 __asm__("r4") = e;
    register long r5 __asm__("r5") = f;
    __asm__ volatile("svc 0" : "+r"(r0)
                     : "r"(r7), "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5)
                     : "memory");
    return r0;
}

/* ==========================================================================
 * logging -- into RAM only. There is no /dev/console in this ramdisk, so
 * write(1,...) goes nowhere. Kept because a debugger core dump or a
 * /proc/pid/mem read could recover it; not the primary channel.
 * ======================================================================== */

#define LOGMAX 4096
static char g_log[LOGMAX];
static int  g_loglen;

static void say(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    sc(SYS_write, 1, (long)s, n, 0, 0);
    for (int i = 0; i < n && g_loglen < LOGMAX; i++) g_log[g_loglen++] = s[i];
}

static void saynum(long v)
{
    char b[24];
    int n = 0;
    if (v < 0) { say("-"); v = -v; }
    if (v == 0) { say("0"); return; }
    while (v && n < 23) { b[n++] = (char)('0' + (v % 10)); v /= 10; }
    for (int i = n; i > 0; i--) { char c = b[i-1]; say(&c); }
}

static void line(const char *a, long v) { say(a); saynum(v); say("\n"); }
static void nl(void) { say("\n"); }

static void delay(long n) { for (volatile long k = 0; k < n; k++) ; }

/* ==========================================================================
 * SCREEN
 * ======================================================================== */

/* Colour constants. NOTE these were wrong once and the names lied:
 *   C_BLUE  = 0x0000FFFF renders as (255,255,0) = YELLOW, not blue
 * so a report of "stuck on pink" could not be decoded into a state at all.
 * Only magenta and green had ever been verified, and both of those are
 * channel-order invariant (magenta has R==B; green has one channel lit), so
 * those two tests never established the framebuffer byte order either.
 *
 * Stage 7 no longer relies on hue. It uses solid WHITE before the call and a
 * countable black/white BARCODE after it, because a pattern is immune to both
 * byte order and colour perception.
 */
#define C_MAGENTA 0xFFFF00FFu   /* verified: renders magenta */
#define C_GREEN   0xFF00FF00u   /* verified: renders green   */
#define C_WHITE   0xFFFFFFFFu   /* order-invariant           */
#define C_BLACK   0x00000000u   /* order-invariant           */

static int           g_fb = -1;
static u32          *g_base;
static unsigned long g_maplen;
static unsigned long g_plane_words;

static int screen_open(void)
{
    /* open() alone powers the panel on: msm_fb_open() calls
     * msm_fb_blank_sub(FB_BLANK_UNBLANK), which sets panel_power_on. Together
     * with op_enable (TRUE from driver probe) that satisfies both gates of
     * MSMFB_DISPLAY_COMMIT. The EPERM seen under TWRP was its screen saver
     * having blanked the panel, not a missing bring-up by us. */
    g_fb = (int)sc(SYS_open, (long)"/dev/graphics/fb0", O_RDWR, 0, 0, 0);
    if (g_fb < 0) return 0;

    unsigned long map_len = FB_STRIDE * (unsigned long)FB_H * 2;
    map_len = (map_len + PAGE - 1) & ~(unsigned long)(PAGE - 1);
    long addr = sc6(SYS_mmap2, 0, (long)map_len,
                    PROT_READ | PROT_WRITE, MAP_SHARED, g_fb, 0);
    if ((unsigned long)addr >= 0xFFFFF000u) return 0;
    g_base = (u32 *)addr;
    g_maplen = map_len;
    g_plane_words = FB_STRIDE * (unsigned long)FB_H / 4;
    return 1;
}

/* Fill BOTH planes. Committing an unfilled plane produced a blank screen once. */
static void screen_fill(u32 colour)
{
    for (unsigned long i = 0; i < g_plane_words; i++) g_base[i] = colour;
    for (unsigned long i = 0; i < g_plane_words; i++) g_base[g_plane_words + i] = colour;
}

static int screen_commit(void)
{
    static struct mdp_display_commit dc;
    for (unsigned long i = 0; i < sizeof(dc) / 4; i++) ((u32 *)&dc)[i] = 0;
    dc.flags = 0;
    dc.wait_for_finish = 1;
    dc.var.xres = FB_W;
    dc.var.yres = FB_H;
    dc.var.xres_virtual = FB_W;
    dc.var.yres_virtual = FB_H * 2;
    dc.var.xoffset = 0;
    dc.var.yoffset = 0;
    dc.var.bits_per_pixel = 32;
    return (int)sc(SYS_ioctl, g_fb, MSMFB_DISPLAY_COMMIT, (long)&dc, 0, 0);
}

static void paint(u32 colour)
{
    screen_fill(colour);
    screen_commit();
}

/* Draw |v| white bars on black, 64 slots across the screen.
 *
 * The point is that a human counts them. After three stage-7 runs whose only
 * output was a hue, "pink" was reported and could not be mapped to any state --
 * because C_BLUE had been yellow all along. A stripe pattern cannot be
 * misread, and cannot be distorted by framebuffer byte order, because it is
 * not a colour at all.
 *
 * Typical readings:  1 bar  = -EPERM   (no CAP_SYS_BOOT)
 *                  22 bars = -EINVAL  (magic rejected)
 *                   0 bars = rc == 0, returned success without powering off
 */
/* Sleep using poll(), not a busy-loop counter.
 *
 * The flashing elsewhere already runs off poll()'s 200 ms timeout, so it is
 * the only clock on this platform with known units. `delay()` is an uncalibrated
 * spin loop and its period is a guess; timing a human-visible hold with it would
 * make "30 seconds" a fiction. poll() with fd -1 and events 0 is a plain sleep.
 * (struct pollfd is declared with the touch code further down.)
 */

/* Sleep using poll(), not a busy-loop counter. Declared here because the
 * display code needs it and the definition sits with the touch code below. */
static void sleep_ms(long ms);

/* ==========================================================================
 * TEXT
 *
 * The result screen spent several runs hand-encoding values as bar counts, and
 * every round produced a new ambiguity: a popcount read as a count, a
 * placeholder read as an errno, a hexdump read as a step number. The encoding
 * was the failure, not the reader.
 *
 * So the screen now says what it means. The font is rasterised from a system
 * TrueType face by gen-font.py -- 64 glyphs, 320 bytes -- rather than
 * transcribed, which is the same discipline as vendoring reboot.h instead of
 * recalling 0x4321.
 * ========================================================================== */

#include "font5x7.h"

/* FB_STRIDE is 4352 BYTES and the panel is 32bpp, so a row is 1088 u32 = 1088
 * pixels, of which FB_W = 1080 are visible. Scale 7 gives a 35x49 px glyph and
 * the longest line (23 chars) needs 23 * 6 * 7 = 966 px, which fits inside the
 * 1016 px left after margins. Scale 8 would need 1104 px and overflow. */
#define TEXT_SCALE   7
#define TEXT_LEFT    PAT_MARGIN_WORDS       /* 32 px, same margin as the bars */
#define TEXT_Y0      250
#define TEXT_DY      300

/* Tripwire: the widest line we print must fit the visible width. 23 chars is
 * "10 60 00 00 02 00 01 00 " -- the full 8-byte response. */
typedef char tripwire_text_fits[
    (23 * FONT_CELL_W * TEXT_SCALE <= FB_W - 2 * PAT_MARGIN_WORDS) ? 1 : -1];

static void fill_rect(long x0, long y0, long w, long h, u32 colour)
{
    const long wpr = FB_STRIDE / 4;
    for (long y = y0; y < y0 + h; y++) {
        if (y < 0 || y >= FB_H) continue;
        u32 *row = &g_base[y * wpr];
        for (long x = x0; x < x0 + w; x++) {
            if (x < 0 || x >= wpr) continue;
            row[x] = colour;
            g_base[g_plane_words + y * wpr + x] = colour;
        }
    }
}

static void draw_text(long px, long py, const char *s, long scale, u32 fg, u32 bg)
{
    long cx = px;
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch < FONT_FIRST) ch = ' ';
        const unsigned char *g = font5x7[ch - FONT_FIRST];
        for (long col = 0; col < 5; col++)
            for (long row = 0; row < 7; row++) {
                if (!(g[col] & (1u << row))) continue;
                fill_rect(cx + col * scale, py + row * scale,
                          scale, scale, fg);
            }
        /* erase the inter-glyph gutter so text never looks joined up */
        fill_rect(cx + 5 * scale, py, scale, 7 * scale, bg);
        cx += FONT_CELL_W * scale;
    }
}

/* --- minimal formatting; no libc, no snprintf --- */
static char *put_str(char *p, const char *s)
{
    while (*s) *p++ = *s++;
    return p;
}

static char *put_dec(char *p, long v)
{
    char tmp[12];
    int n = 0;
    if (v < 0) { *p++ = '-'; v = -v; }
    if (v == 0) { *p++ = '0'; return p; }
    while (v) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) *p++ = tmp[--n];
    return p;
}

static char *put_hex2(char *p, unsigned v)
{
    static const char h[] = "0123456789ABCDEF";
    *p++ = h[(v >> 4) & 0xF];
    *p++ = h[v & 0xF];
    return p;
}

/* ---- pattern geometry --------------------------------------------------
 * Slots are DISCRETE: each is a black bar followed by a gap. A contiguous run
 * of black cannot be counted -- the user could only report "about 1/10 of the
 * width", which is how the earlier runs were ambiguous.
 *
 * The first discrete-bar build used 136 slots of 8 words, giving 16 px bars.
 * The user reported those as "too thin to count exactly", which is the correct
 * verdict: 16 px on a 1080 px panel is not countable by eye at arm's length.
 *
 * So the slot count is traded for width. A row is FB_STRIDE/4 = 1088 words and
 * 1088 / 16 = 68 slots exactly:
 *
 *   slot = 16 words = 64 px  ->  bar = 11 words = 44 px, gap = 5 words = 20 px
 *
 * 44 px bars are unambiguous. The range is 68 rather than 136, which still
 * covers every errno we can plausibly see here -- EPERM 1, EIO 5, ENODEV 19,
 * EINVAL 22, ENOSYS 38 -- and if a larger value ever appears the display reads
 * as a full row of 68 bars rather than lying about a smaller number.
 */
#define PAT_SLOTS      64
#define PAT_SLOT_WORDS 16
#define PAT_BAR_WORDS  11
/* PAT_MARGIN_WORDS (32 px each side) is defined with the framebuffer
 * constants so the text renderer can use it too. It exists because the bars
 * were hard to read "when they are so close to the edge of the screen". */
typedef char tripwire_pattern_geometry[
    (PAT_MARGIN_WORDS * 2 +
     PAT_SLOTS * PAT_SLOT_WORDS == FB_STRIDE / 4) ? 1 : -1];

/* Draw the slot pattern between rows [y0,y1]. slot[i] != 0 draws one bar.
 * A full row of bars means the value was >= PAT_SLOTS and was clamped. */

/* Draw the slot pattern between rows [y0,y1]. slot[i] != 0 draws one bar. */
static void draw_pattern(const u8 slot_on[PAT_SLOTS], u32 bg, u32 fg,
                         long y0, long y1)
{
    /* BUG FIXED HERE. The first version computed the slot width in PIXELS
     * (`FB_STRIDE / 64` = 68) but then indexed a u32* row with it, where the
     * unit is WORDS. A row is only FB_STRIDE/4 = 1088 words, so
     * `row[i * 68 + x]` ran up to index 4351 -- four rows past the end of the
     * row it was supposed to fill. Every row was overwritten four times and
     * the slot that survived was 48..63, so with any realistic count the
     * whole screen came out background and a FAILURE looked like SUCCESS.
     *
     * 32 + 64*16 + 32 = 1088 words, exactly one row.
     */
    const long words_per_row = FB_STRIDE / 4;          /* 1088 */

    for (long y = y0; y <= y1; y++) {
        u32 *row = &g_base[y * words_per_row];
        for (long i = 0; i < PAT_SLOTS; i++) {
            u32 *slot = &row[PAT_MARGIN_WORDS + i * PAT_SLOT_WORDS];
            for (long x = 0; x < PAT_SLOT_WORDS; x++)
                slot[x] = (slot_on[i] && x < PAT_BAR_WORDS) ? fg : bg;
        }
    }
    /* mirror the same rows into plane 2 so a commit can never show an
     * unfilled plane */
    for (long y = y0; y <= y1; y++)
        for (long i = 0; i < words_per_row; i++)
            g_base[g_plane_words + y * words_per_row + i] =
                g_base[y * words_per_row + i];
    screen_commit();
}

/* Hold the current pattern still, with a shrinking countdown bar below it, so
 * the result can actually be read off the screen.
 *
 * The first green frame previously survived only until the first flash tick
 * (~200 ms), which is far too short to count 64 slots. The user asked for a
 * longer hold; a fixed silent delay would be indistinguishable from a hang, so
 * the remaining time is drawn as a bar that empties left to right.
 *
 * The pattern occupies rows 0..1500 and the countdown rows 1600..1700, so the
 * bar never overwrites a slot.
 */
#define RESULT_ROW_BOT  1500
#define HOLD_ROW_TOP    1600
#define HOLD_ROW_BOT    1700

/* Two disjoint bands on the result screen:
 *   rows    0..700  the RESULT CODE, as a unary count
 *   rows 800..1500  the RESPONSE BYTES, as a hex dump (only when there are any)
 *
 * They must not share rows. A popcount of 3 and a unary count of 3 draw the
 * identical picture, which is how "3 bars" was misread as "ioctl WAKE_CTL
 * failed" when in fact it was a hexdump of whatever the controller sent. */
#define BAND_CODE_TOP     0
#define BAND_CODE_BOT   350
#define BAND_ERRNO_TOP  400
#define BAND_ERRNO_BOT  700
#define BAND_BYTES_TOP  800
#define BAND_BYTES_BOT 1500

static void hold_with_countdown(long secs, u32 bg)
{
    const long wpr = FB_STRIDE / 4;
    const long ticks = secs * 4;              /* 250 ms per tick */

    for (long t = ticks; t > 0; t--) {
        long filled = (wpr * t) / ticks;
        for (long y = HOLD_ROW_TOP; y <= HOLD_ROW_BOT; y++) {
            u32 *row = &g_base[y * wpr];
            for (long x = 0; x < wpr; x++)
                row[x] = (x < filled) ? C_WHITE : bg;
            for (long x = 0; x < wpr; x++)
                g_base[g_plane_words + y * wpr + x] = row[x];
        }
        screen_commit();
        sleep_ms(250);
    }
    /* clear the countdown strip so the flash that follows starts clean */
    for (long y = HOLD_ROW_TOP; y <= HOLD_ROW_BOT; y++)
        for (long x = 0; x < wpr; x++) {
            g_base[y * wpr + x] = bg;
            g_base[g_plane_words + y * wpr + x] = bg;
        }
    screen_commit();
}

/* Unary count: the first `v` slots are `fg`. 1 bar = EPERM, 22 = EINVAL. */
static void barcode(long v, u32 bg, u32 fg, long y0, long y1)
{
    u8 on[PAT_SLOTS];
    if (v < 0) v = -v;
    if (v > PAT_SLOTS) v = PAT_SLOTS;
    for (int i = 0; i < PAT_SLOTS; i++) on[i] = (i < v);
    draw_pattern(on, bg, fg, y0, y1);
}

/* Hex dump: 8 bars per byte, MSB first, black = bit set. Byte 0 is the first
 * group of eight stripes, so "count the bars in each group of eight" reads the
 * bytes out.
 *
 * WARNING: the bar COUNT here is a popcount, not a value. The MT_RSP header
 * `10 60` has three bits set between it, indistinguishable from a unary count
 * of 3 -- which is exactly the misreading in Entry 63. Code and dump are
 * therefore drawn in separate horizontal bands and never share screen rows. */
static void hexdump(const u8 *b, int n, u32 bg, u32 fg, long y0, long y1)
{
    u8 on[PAT_SLOTS];
    int s = 0;
    /* 8 bars per byte, MSB first. Bar N of a byte is set when bit (7-N) is,
     * so byte 0 is the first group of eight stripes. */
    for (int i = 0; i < n && s + 8 <= PAT_SLOTS; i++)
        for (int bit = 7; bit >= 0 && s < PAT_SLOTS; bit--, s++)
            on[s] = (u8)((b[i] >> bit) & 1);
    while (s < PAT_SLOTS) on[s++] = 0;
    draw_pattern(on, bg, fg, y0, y1);
}

/* ==========================================================================
 * TOUCH
 *
 * A gesture is accepted on EITHER a release after a full down+coords, OR a
 * hold of >= ~1.5s. The hold path matters because the second wait has already
 * failed once: if this driver omits ABS_MT_TRACKING_ID == -1 on lift, a
 * release-only matcher would block forever and look exactly like a power-off
 * failure.
 *
 * While waiting, the screen flashes between `colour` and black once a second.
 * A frozen screen therefore means the process stopped, not that a wait is
 * still running.
 * ======================================================================== */

struct input_event { long sec, usec; u16 type, code; s32 value; };
struct pollfd { int fd; short events, revents; };

static void sleep_ms(long ms)
{
    struct pollfd pfd;
    pfd.fd = -1;                  /* no fds: poll() then just sleeps */
    pfd.events = 0;
    pfd.revents = 0;
    sc(SYS_poll, (long)&pfd, 1, ms, 0, 0);
}

static int wait_touch(int seconds, u32 colour)
{
    static struct input_event ev[64];
    static struct pollfd pfd;

    int fd = (int)sc(SYS_open, (long)"/dev/input/event1",
                     O_RDONLY | O_NONBLOCK, 0, 0, 0);
    if (fd < 0) { line("  event1 open FAILED errno=", -fd); return -1; }

    pfd.fd = fd; pfd.events = 0x0001 /*POLLIN*/; pfd.revents = 0;

    int x = 0, y = 0, down = 0, sx = 0, sy = 0;
    int held_ticks = 0;
    int lit = 0;                       /* which half of the flash we are on */

    /* seconds * 5 half-second ticks, polled at 200ms */
    for (int tick = 0; tick < seconds * 5; tick++) {
        int pr = (int)sc(SYS_poll, (long)&pfd, 1, 200, 0, 0);

        if (pr > 0) {
            long n = sc(SYS_read, fd, (long)ev, sizeof(ev), 0, 0);
            if (n >= (long)sizeof(struct input_event)) {
                int count = (int)(n / (long)sizeof(struct input_event));
                for (int i = 0; i < count; i++) {
                    if (ev[i].type != EV_ABS) continue;
                    if (ev[i].code == ABS_MT_POSITION_X) { x = ev[i].value; sx = 1; }
                    if (ev[i].code == ABS_MT_POSITION_Y) { y = ev[i].value; sy = 1; }
                    if (ev[i].code == ABS_MT_TRACKING_ID) {
                        if (ev[i].value >= 0) { down = 1; sx = sy = 0; held_ticks = 0; }
                        else if (down) {
                            down = 0;
                            if (sx && sy) {            /* full tap */
                                paint(colour);
                                sc(SYS_close, fd, 0, 0, 0, 0);
                                line("  TAP at x=", x);
                                line("       y=", y);
                                return 0;
                            }
                        }
                    }
                }
            }
        }

        /* heartbeat: flip the screen every 5 polls (1s) */
        if (tick % 5 == 0) {
            lit = !lit;
            paint(lit ? colour : C_BLACK);
        }

        if (down && sx && sy && ++held_ticks >= 8) {   /* ~1.6s hold */
            paint(colour);
            sc(SYS_close, fd, 0, 0, 0, 0);
            line("  HOLD at x=", x);
            line("       y=", y);
            return 0;
        }
    }

    paint(colour);
    sc(SYS_close, fd, 0, 0, 0, 0);
    return -1;                                       /* timed out */
}

/* ==========================================================================
 * NFC
 * ======================================================================== */

/* Return 0 only if every step demonstrably worked; otherwise a small code
 * that is drawn on screen as a bar count, so the result becomes observable
 * instead of merely "not crashed".
 *
 * Previously this returned void and the outside world learned nothing: a
 * failed open() and a successful one both looked like silence.
 */
/* Response bytes and length, kept so they can be DRAWN. Reporting only a
 * pass/fail code told us nothing about what the controller actually said. */
static u8  g_nfc_rsp[64];
static int g_nfc_got;
static int g_nfc_errno;   /* |errno| of the first failing call, 0 if none */
static int g_nfc_irq;     /* 1 if the controller ever raised its interrupt */

/* NFC bring-up timing. The driver reports -EIO whenever i2c_master_send()
 * returns fewer bytes than requested, so the only way to tell "the chip was
 * not ready yet" from "our framing is wrong" is to give it time and retry.
 * These are deliberately generous: a retry costs milliseconds and the test
 * runs once. */
#define NFC_PWR_SETTLE_MS   100
#define NFC_WAKE_SETTLE_MS   50
#define NFC_RETRY_MS         50
#define NFC_WRITE_RETRIES    4
#define NFC_POLL_MS         500    /* how long to wait for the chip's IRQ */
#define NFC_READ_TRIES       4

/* Result codes, in the order nfc_test() returns them. Read off the STEP band.
 *
 *   0  all steps succeeded
 *   1  open failed                     errno set
 *   2  POWER_CTL failed                errno set
 *   3  write Core Reset failed         errno set
 *   4  write GET_BUILD_INFO failed     errno set
 *   5  the chip never raised its IRQ   (poll timed out every time)
 *   6  a reply arrived with an unexpected header; bytes are shown
 *   7  the chip DID raise its IRQ but the I2C read still returned nothing
 *
 * 5 and 7 are the pair that matters: both mean "read() gave us nothing", but
 * only 7 means the controller replied and we failed to collect it. */
#define NFC_OK          0
#define NFC_FAIL_OPEN   1
#define NFC_FAIL_POWER  2
#define NFC_FAIL_WR1    3
#define NFC_FAIL_WR2    4
#define NFC_SILENT      5
#define NFC_BAD_HEADER  6
#define NFC_READ_FAILED 7

static int nfc_test(void)
{
    g_nfc_errno = 0;
    g_nfc_irq = 0;

    long r = sc(SYS_open, (long)"/dev/bcm2079x", O_RDWR | O_NOCTTY, 0, 0, 0);
    if (r < 0) { g_nfc_errno = (int)-r; return NFC_FAIL_OPEN; }
    int fd = (int)r;
    say("  bcm2079x opened\n");

    /* The raw return value is kept, not just its sign. A mapped code such as
     * "3 = WAKE_CTL failed" is a guess about the kernel; the errno is the
     * kernel telling us. Codes 2 and 3 were believed unreachable because
     * drivers/nfc/bcm2079x.c in OUR tree returns 0 on every path -- but that
     * tree is not the tree these kernels were built from (Entry 49/55), so
     * the driver's behaviour here cannot be assumed. */
    r = sc(SYS_ioctl, fd, BCMNFC_POWER_CTL, 1, 0, 0);
    if (r < 0) { g_nfc_errno = (int)-r; return NFC_FAIL_POWER; }

    /* Settle after power-on. The chip is on I2C only once its regulator and
     * clock have come up; the first write issued microseconds after EN is
     * asserted is not guaranteed to be answered.
     *
     * This is the fix for the EIO that four runs reported. The driver turns a
     * short i2c_master_send() into -EIO unconditionally, so "5 bars in the
     * errno band" means the bus transfer did not complete -- not that our
     * framing was wrong. */
    sleep_ms(NFC_PWR_SETTLE_MS);

    /* WAKE_CTL is deliberately NOT asserted before the first exchange.
     * b/nfc/TRANSPORT.md records the working HAL's state as
     * `NFC_WAKE_ASSERTED_ON_POR == UPIO_OFF`, i.e. wake is deasserted at
     * power-on, and the HAL only asserts it later in its own state machine.
     * Asserting wake first was our own addition and may have been talking to a
     * chip that was not ready to answer. */
    sleep_ms(NFC_WAKE_SETTLE_MS);

    /* Core Reset, then GET_BUILD_INFO. The working Phase 0 probe
     * (b/phase0/nfc-probe.c) sends exactly these byte sequences.
     * The raw syscall return IS the errno: the kernel returns -errno directly
     * and there is no libc wrapper to flatten it into -1. */
    static const u8 core_reset[] = { 0x10, 0x60, 0x00, 0x02, 0x00, 0x01 };
    static const u8 get_build[]  = { 0x10, 0x2F, 0x04, 0x00 };

    r = -1;
    for (int attempt = 0; attempt < NFC_WRITE_RETRIES; attempt++) {
        r = sc(SYS_write, fd, (long)core_reset, sizeof(core_reset), 0, 0);
        if (r >= 0) break;
        sleep_ms(NFC_RETRY_MS);
    }
    if (r < 0) { g_nfc_errno = (int)-r; return NFC_FAIL_WR1; }

    r = -1;
    for (int attempt = 0; attempt < NFC_WRITE_RETRIES; attempt++) {
        r = sc(SYS_write, fd, (long)get_build, sizeof(get_build), 0, 0);
        if (r >= 0) break;
        sleep_ms(NFC_RETRY_MS);
    }
    if (r < 0) { g_nfc_errno = (int)-r; return NFC_FAIL_WR2; }

    for (int i = 0; i < 8; i++) g_nfc_rsp[i] = 0;
    long got = 0;
    int saw_irq = 0;

    /* Wait for the IRQ before reading. This is the missing step.
     *
     * bcm2079x_dev_poll() reports POLLIN only when count_irq > 0, i.e. when
     * the chip has actually pulled its interrupt line. bcm2079x_dev_read() is
     * a SINGLE i2c_master_recv of 4 bytes -- it does not block and it does not
     * wait for the chip. So issuing read() straight after write() asks the
     * controller for a reply it has not produced yet, and the driver honestly
     * returns 0.
     *
     * The HAL waits on poll() and only then reads. We did not, which is why
     * four runs ended in "no response" once the writes started succeeding.
     *
     * Tracking whether poll ever reported POLLIN separates the two possible
     * causes, which look identical from the read alone:
     *   - poll times out  : the chip never responded at all
     *   - poll fires      : the chip responded but the I2C read still failed */
    for (int i = 0; i < NFC_READ_TRIES; i++) {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = 0x0001;                 /* POLLIN */
        pfd.revents = 0;
        long pr = sc(SYS_poll, (long)&pfd, 1, NFC_POLL_MS, 0, 0);
        if (pr <= 0) continue;               /* timed out; chip still silent */
        saw_irq = 1;
        got = sc(SYS_read, fd, (long)g_nfc_rsp, sizeof(g_nfc_rsp), 0, 0);
        if (got > 0) break;
        sleep_ms(10);
    }
    line("  read bytes=", got);
    g_nfc_irq = saw_irq;
    line("  saw_irq=", saw_irq);

    g_nfc_errno = (got < 0) ? (int)-got : 0;   /* 0 when there is no errno */
    sc(SYS_close, fd, 0, 0, 0, 0);
    g_nfc_got = (int)got;

    if (got > 0) {
        /* The known-good answer begins with an MT_RSP header (0x10 0x60). */
        if (got < 2 || g_nfc_rsp[0] != 0x10 || g_nfc_rsp[1] != 0x60) return NFC_BAD_HEADER;
        return NFC_OK;
    }
    /* No placeholder errno here. An earlier revision wrote
     * `g_nfc_errno = 1` on this path, which is indistinguishable from a real
     * EPERM on the display -- the same fabrication as Entry 65. */
    return saw_irq ? NFC_READ_FAILED : NFC_SILENT;
}

/* How long the NFC result frame is held before the green flash starts.
 * The first run that produced a real reading was cut off mid-sentence
 * ("... 10 ...") because 30 s was not enough to read three lines. 90 s is
 * deliberately excessive: this runs once, and the whole point of the screen
 * is that the only observer is a human with no console. */
#define RESULT_HOLD_SECS 90

/* ==========================================================================
 * main
 * ======================================================================== */

void _start(void)
{
    static char stack[32768] __attribute__((aligned(16)));
    long sp = (long)(stack + sizeof(stack));
    sp &= ~15L; sp -= 16;
    __asm__ volatile("mov sp, %0" :: "r"(sp) : "memory");

    say("\n=== standalone test v2: screen is the only output ===\n");
    line("pid=", sc(SYS_getpid, 0, 0, 0, 0, 0));

    sc(SYS_mkdir, (long)"/proc", 0755, 0, 0, 0);
    sc(SYS_mount, (long)"proc", (long)"/proc", (long)"proc", 0, 0);
    sc(SYS_mkdir, (long)"/sys", 0755, 0, 0, 0);
    sc(SYS_mount, (long)"sysfs", (long)"/sys", (long)"sysfs", 0, 0);


      /* ---- devtmpfs ----
       *
       * The camera's device nodes cannot be baked into the initramfs the
       * way fb0, event1 and bcm2079x are, because its char-device major is
       * allocated at RUNTIME:
       *
       *   msm_camera.c:  alloc_chrdev_region(&msm_devno, 0,
       *                                     4 * MSM_MAX_CAMERA_SENSORS, ...)
       *
       * devtmpfs creates a node for every registered device with the right
       * major, so it solves both the camera (/dev/video0..2) and adb
       * (/dev/ffs.ep0).
       *
       * CONFIG_DEVTMPFS_MOUNT is deliberately NOT set. In 3.4 the automount
       * is in prepare_namespace(), and kernel_init() runs
       * run_init_process() -- us -- BEFORE that. So for an initramfs the
       * automount would arrive after we had already started, which is
       * useless. We mount it ourselves.
       *
       * The fallback matters: if the mount fails, or succeeds but the
       * framebuffer is not there, we UNDO it and carry on with the baked
       * cpio nodes, which is the configuration that has already proved the
       * screen, the touch and the NFC work. devtmpfs does not change the
       * fb0 path either: the MSM driver uses class "graphics", so the node
       * is /dev/graphics/fb0 under devtmpfs exactly as it is in the cpio.
       */
      sc(SYS_mkdir, (long)"/dev", 0755, 0, 0, 0);
      long dm = sc(SYS_mount, (long)"devtmpfs", (long)"/dev",
                   (long)"devtmpfs", 0, 0);
      int fbprobe = (int)sc(SYS_open, (long)"/dev/graphics/fb0", O_RDONLY,
                            0, 0, 0);
      if (fbprobe >= 0) {
          sc(SYS_close, fbprobe, 0, 0, 0, 0);
      } else if (dm == 0) {
          /* mounted, but no framebuffer: undo it and use the baked nodes */
          sc(SYS_umount2, (long)"/dev", 0, 0, 0, 0);
          dm = -1;
      }
      line("  devtmpfs mount=", dm);

    /* ---- 2. screen ---- */
    if (!screen_open()) {
        /* no display means no diagnostics at all; flash black and stop */
        for (;;) delay(1000000L);
    }
    paint(C_MAGENTA);
    say("  stage 2 MAGENTA committed\n");

    /* ---- 3. first touch (flashing magenta) ---- */
    say("  stage 3: tap to continue\n");
    line("  wait_touch1 -> ", wait_touch(120, C_MAGENTA));

    /* ---- 4. NFC ----
     * The result is painted, not logged: solid green means the controller
     * answered, and green with N black bars names the step that failed
     * (1 open, 2 POWER_CTL, 3 WAKE_CTL, 4/5 write, 6 no response,
     * 7 unexpected response). "It did not crash" is not evidence.
     */
    say("  stage 4: NFC\n");
    int nfc = nfc_test();
    line("  nfc result=", nfc);

    /* ---- 5. green ---- */
    paint(C_GREEN);
    {
        char buf[64], *p;

        p = put_str(buf, "NFC STEP ");
        p = put_dec(p, nfc);
        p = put_str(p, (nfc == NFC_OK) ? " OK   ERRNO " : " FAIL ERRNO ");
        p = put_dec(p, g_nfc_errno);
        *p = 0;
        draw_text(TEXT_LEFT, TEXT_Y0, buf, TEXT_SCALE, C_BLACK, C_GREEN);

        p = put_str(buf, "IRQ ");
        p = put_dec(p, g_nfc_irq);
        p = put_str(p, "  BYTES ");
        p = put_dec(p, g_nfc_got);
        *p = 0;
        draw_text(TEXT_LEFT, TEXT_Y0 + TEXT_DY, buf, TEXT_SCALE, C_BLACK, C_GREEN);

        p = buf;
        for (int i = 0; i < 8; i++) p = put_hex2(p, g_nfc_rsp[i]), *p++ = ' ';
        *p = 0;
        draw_text(TEXT_LEFT, TEXT_Y0 + 2*TEXT_DY, buf, TEXT_SCALE, C_BLACK, C_GREEN);

        say("  stage 5 GREEN committed -- holding result for reading\n");
    }

    /* Hold the frame still while it is read. Previously it survived only until
     * the first flash tick, roughly 200 ms, which is not enough time to count
     * 64 slots off a screen. The countdown bar below the pattern makes the
     * pause obviously deliberate rather than a hang. */
    hold_with_countdown(RESULT_HOLD_SECS, C_GREEN);

    /* ---- 6. second touch (flashing green) ---- */
    say("  stage 6: tap to continue\n");
    line("  wait_touch2 -> ", wait_touch(120, C_GREEN));

    /* ---- 7. power off ----
     * WHITE is painted BEFORE the call. If the screen freezes on WHITE, the
     * kernel took the call and stopped the CPU without cutting the rails.
     * If a BARCODE appears, reboot() returned and the number of bars is the
     * absolute errno. No hue is involved, so neither framebuffer byte order
     * nor colour perception can corrupt the reading.
     */
    paint(C_WHITE);
    say("  stage 7 WHITE: calling reboot(magic1, magic2, POWER_OFF)\n");

    long rc = sc(SYS_reboot, (long)LINUX_REBOOT_MAGIC1,     /* magic1 */
                          (long)LINUX_REBOOT_MAGIC2,     /* magic2 */
                          LINUX_REBOOT_CMD_POWER_OFF,    /* cmd     */
                          0, 0);
    line("  reboot() RETURNED rc=", rc);

    barcode(rc, C_BLACK, C_WHITE, 0, FB_H - 1);
    say("  barcode drawn: count the bars = |rc|\n");
    for (;;) delay(1000000L);
}