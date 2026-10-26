/*
 * touch-ui.c -- draw something you can SEE, and power off when you TOUCH it.
 *
 * WHY THIS EXISTS
 *
 * The capability probe (caps-probe.c) proved /dev/graphics/fb0 accepts writes
 * and that the data reads back byte-identical. That is a hardware fact, not a
 * demonstration: nobody has actually looked at the panel and seen our output.
 * If the display controller is scanning a buffer we are not writing, or TWRP's
 * recovery UI repaints over us a frame later, every one of those "PASS" results
 * would still be true and the screen would still show TWRP.
 *
 * So this program is deliberately obvious:
 *
 *   - it fills the whole panel with a colour you cannot confuse with TWRP,
 *   - it draws a marker at the coordinates of your finger and prints them,
 *   - TAP to move the marker, HOLD for ~1.5 s to power the device off.
 *
 * That exercises the display and the touchscreen together and ends with the
 * power-off path, so all three are demonstrated in one interaction.
 *
 * WHY TWRP MUST BE STOPPED FIRST
 *
 * TWRP's `recovery` service draws its own UI to the same framebuffer, and init
 * will happily repaint over us. The .rc stops it before starting this; see
 * init.touchui.rc. Stopping it does not affect adb -- adbd is a separate
 * init service -- so the debug channel stays.
 *
 * BUFFER NOTE
 *
 * /dev/graphics/fb0 is 1080x1920 with a STRIDE OF 4352, not width*4 = 4320.
 * The 32-byte-per-row difference is the single most likely way to get a
 * skewed or invisible image. We paint both buffer 0 and buffer 1 because we do
 * not know which one the panel is scanning, and painting both costs nothing.
 *
 * TEXT
 *
 * A 5x7 bitmap font for digits and a few symbols, scaled 3x. Real text needs a
 * real font -- Krux already has the pipeline for that (firmware/font/*.bdf plus
 * bdftokff.py) -- but for a proof of life digits and symbols are enough, and
 * they carry no risk of a mistyped glyph table.
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int  u32;
typedef int           s32;

/* ---- flags ------------------------------------------------------------- */
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_CREAT     0x0040
#define O_TRUNC     0x0200
#define O_NONBLOCK  0x0800
#define O_NOCTTY    0x0400

/* ---- syscall numbers (asm/unistd-eabi.h, verified in the disassembly) -- */
#define SYS_exit      1
#define SYS_read      3
#define SYS_write     4
#define SYS_open      5
#define SYS_close     6
#define SYS_execve   11
#define SYS_ioctl    54
#define SYS_poll    168
#define SYS_pread64 180
#define SYS_pwrite64 181
#define SYS_statfs   99
#define SYS_fsync   118
#define SYS_reboot   88      /* 142 is _newselect, NOT reboot */

/* ---- framebuffer ------------------------------------------------------- */
#define FB_W        1080
#define FB_H        1920
#define FB_STRIDE   4352                 /* NOT 1080*4 */
#define FB_ROWU     (FB_STRIDE / 4)      /* 1088 words  */
#define FB_NBUF     2                    /* buffer 2 does not exist: EFBIG */

/* ---- touch codes (getevent -pi /dev/input/event1) --------------------- */
#define EV_ABS                 0x03
#define ABS_MT_SLOT            0x2f   /* 0..9    */
#define ABS_MT_TOUCH_MAJOR     0x30   /* 0..15   */
#define ABS_MT_POSITION_X      0x35   /* 0..1079 */
#define ABS_MT_POSITION_Y      0x36   /* 0..1919 */
#define ABS_MT_TRACKING_ID     0x39   /* 0..65535, -1 = released */
#define ABS_MT_PRESSURE        0x3a   /* 0..255  */

/* ---- behaviour --------------------------------------------------------- */
#define HOLD_POLLS   3      /* consecutive polls with the finger down before
                             * we power off. poll timeout is 500 ms, so this is
                             * ~1.5 s. */
#define TAP_TOUCHES  3      /* taps before HOLD_POLLS is required */

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

/* Six-argument form: 64-bit offset lives in the register PAIR (r3=lo, r4=hi). */
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
 * console echo (stdout is adb, not the panel -- the panel is drawn below)
 * ======================================================================== */

static void say(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    sc(SYS_write, 1, (long)s, n, 0, 0);
}

static void saynum(long v)
{
    char b[24];
    int n = 0;
    if (v < 0) { sc(SYS_write, 1, (long)"-", 1, 0, 0); v = -v; }
    if (v == 0) { sc(SYS_write, 1, (long)"0", 1, 0, 0); return; }
    while (v && n < 23) { b[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n) sc(SYS_write, 1, (long)&b[--n], 1, 0, 0);
}

/* ==========================================================================
 * a full-screen image in RAM, flushed only when something changes
 *
 * 1080*1920*4 = 8.3 MB of .bss. The device has 2 GB, so this is free, and it
 * makes drawing a matter of poking one array instead of doing a read-modify-
 * write on the framebuffer for every rectangle.
 * ======================================================================== */

static u32 g_img[FB_H * FB_ROWU];
static int g_fb = -1;

static u32 rgb(u32 r, u32 g, u32 b) { return 0xFF000000u | (b << 16) | (g << 8) | r; }

static void img_clear(u32 c)
{
    long n = (long)FB_H * FB_ROWU;
    for (long i = 0; i < n; i++) g_img[i] = c;
}

/* Push rows [y0,y1) of our image to buffer `buf` of the real framebuffer. */
static void flush_rows(int buf, int y0, int y1)
{
    if (g_fb < 0) return;
    if (y0 < 0) y0 = 0;
    if (y1 > FB_H) y1 = FB_H;
    for (int y = y0; y < y1; y++) {
        long off = (long)buf * (FB_STRIDE * FB_H) + (long)y * FB_STRIDE;
        sc6(SYS_pwrite64, g_fb, (long)(g_img + (long)y * FB_ROWU),
            FB_STRIDE, off, 0, 0);
    }
}

static void flush_both(int y0, int y1)
{
    for (int b = 0; b < FB_NBUF; b++) flush_rows(b, y0, y1);
}

/* A filled rectangle, with soft edges so it reads as a marker not a blob. */
static void rect(int x, int y, int w, int h, u32 c)
{
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= FB_H) continue;
        for (int i = 0; i < w; i++) {
            int xx = x + i;
            if (xx < 0 || xx >= FB_W) continue;
            g_img[(long)yy * FB_ROWU + xx] = c;
        }
    }
}

static void frame(int x, int y, int w, int h, int t, u32 c)
{
    rect(x, y, w, t, c);
    rect(x, y + h - t, w, t, c);
    rect(x, y, t, h, c);
    rect(x + w - t, y, t, h, c);
}

/* A crosshair, so the exact finger position is unambiguous. */
static void crosshair(int x, int y, u32 c)
{
    rect(x - 40, y - 2, 80, 4, c);
    rect(x - 2, y - 40, 4, 80, c);
    for (int d = 0; d < 26; d++) {
        rect(x + d, y + d, 3, 3, c);
        rect(x - d, y + d, 3, 3, c);
        rect(x + d, y - d, 3, 3, c);
        rect(x - d, y - d, 3, 3, c);
    }
}

/* ==========================================================================
 * 5x7 font: digits plus '=', '-', ':' and space. Scaled 3x => 15x21 pixels.
 * ======================================================================== */

static const u8 FONT[14][7] = {
/* '0' */ {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
/* '1' */ {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
/* '2' */ {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
/* '3' */ {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},
/* '4' */ {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
/* '5' */ {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
/* '6' */ {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
/* '7' */ {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
/* '8' */ {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
/* '9' */ {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
/* '=' */ {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00},
/* '-' */ {0x00,0x00,0x00,0x1F,0x00,0x00,0x00},
/* ':' */ {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00},
/* ' ' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};

static int glyph(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c == '=') return 10;
    if (c == '-') return 11;
    if (c == ':') return 12;
    return 13;                                  /* anything else: blank */
}

#define GLYPH_W 5
#define GLYPH_H 7
#define SCALE   3
#define TEXT_W  (GLYPH_W * SCALE)
#define TEXT_H  (GLYPH_H * SCALE)

/* Draw text with its top-left at (x,y). Returns the x just past the end. */
static int text(int x, int y, const char *s, u32 c)
{
    for (; *s; s++) {
        int g = glyph(*s);
        for (int row = 0; row < GLYPH_H; row++)
            for (int col = 0; col < GLYPH_W; col++)
                if (FONT[g][row] & (1 << (GLYPH_W - 1 - col)))
                    rect(x + col * SCALE, y + row * SCALE, SCALE, SCALE, c);
        x += TEXT_W + SCALE;
    }
    return x;
}

/* Right-align a number so the digits do not jitter as the value changes. */
static void text_num(int x, int y, long v, int digits, u32 c)
{
    char buf[12];
    int n = 0;
    int neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    do { buf[n++] = (char)('0' + (v % 10)); v /= 10; } while (v && n < 11);
    while (n < digits) buf[n++] = '0';           /* pad, not left-truncate */
    char out[16];
    int m = 0;
    if (neg) out[m++] = '-';
    for (int i = n - 1; i >= 0; i--) out[m++] = buf[i];
    out[m] = 0;
    text(x, y, out, c);
}

/* ==========================================================================
 * screen state
 * ======================================================================== */

#define BG        0xFF102040u    /* dark blue   -- nothing like TWRP        */
#define PANEL     0xFF000018u    /* near black panel behind the readout   */
#define FG        0xFF30FF60u    /* bright green digits                     */
#define ACCENT    0xFFFF6600u    /* orange header bar                       */
#define MARKER    0xFFFFFFFFu    /* white crosshair                         */

static void draw_chrome(void)
{
    rect(0, 0, FB_W, 120, ACCENT);
    rect(0, FB_H - 160, FB_W, 160, ACCENT);
    rect(40, 200, FB_W - 80, 520, PANEL);
    frame(40, 200, FB_W - 80, 520, 4, ACCENT);
}

static void draw_readout(int x, int y, int taps)
{
    /* X and Y, zero-padded so the digits stay put while the finger moves. */
    text(80, 250, "X:", FG);
    text_num(80 + 3 * (TEXT_W + SCALE), 250, x, 4, FG);
    text(80, 320, "Y:", FG);
    text_num(80 + 3 * (TEXT_W + SCALE), 320, y, 4, FG);
    text(80, 400, "T:", FG);
    text_num(80 + 3 * (TEXT_W + SCALE), 400, taps, 2, FG);
}

static void draw_footer(int taps)
{
    text(40, FB_H - 110, "TAP: MOVE", FG);
    if (taps >= TAP_TOUCHES)
        text(600, FB_H - 110, "HOLD: OFF", MARKER);
    else
        text(600, FB_H - 110, "- - -", FG);
}

/* ==========================================================================
 * power off
 *
 * reboot(2) returns EINVAL to a non-init process on this CAF kernel, so use the
 * Android mechanism instead: TWRP's init.rc has
 *     on property:sys.powerctl=*
 *         powerctl ${sys.powerctl}
 * and we run as a service under init, so we simply ask init to do it.
 * ======================================================================== */

static void power_off(const char *why)
{
    say("  powering off: ");
    say(why);
    say("\n");

    /* Try reboot(2) first: it is the direct route if the kernel allows it. */
    sc(SYS_reboot, 0x4321L, 0x28121969L, 0, 0, 0);

    static char *argv[] = { (char *)"/sbin/setprop",
                            (char *)"sys.powerctl",
                            (char *)"shutdown", 0 };
    static char *envp[] = { (char *)"PATH=/sbin:/system/bin", 0 };
    sc(SYS_execve, (long)argv[0], (long)argv, (long)envp, 0, 0);

    say("  power-off FAILED (neither reboot nor setprop worked)\n");
}

/* ==========================================================================
 * main
 * ======================================================================== */

struct input_event {
    long sec, usec;
    u16  type;
    u16  code;
    s32  value;
};

struct pollfd { int fd; short events; short revents; };

int main(void)
{
    say("touch-ui: starting\n");

    /* ---- framebuffer ------------------------------------------------- */
    g_fb = (int)sc(SYS_open, (long)"/dev/graphics/fb0", O_RDWR, 0, 0, 0);
    if (g_fb < 0) {
        say("touch-ui: cannot open /dev/graphics/fb0\n");
        sc(SYS_exit, 1, 0, 0, 0, 0);
    }
    say("touch-ui: framebuffer open\n");

    /* ---- touchscreen -------------------------------------------------- */
    int tty = (int)sc(SYS_open, (long)"/dev/input/event1",
                      O_RDONLY | O_NONBLOCK, 0, 0, 0);
    if (tty < 0) {
        say("touch-ui: cannot open /dev/input/event1\n");
        sc(SYS_exit, 1, 0, 0, 0, 0);
    }
    say("touch-ui: touchscreen open\n");

    /* ---- paint -------------------------------------------------------- */
    img_clear(BG);
    draw_chrome();
    draw_readout(0, 0, 0);
    draw_footer(0);
    text(80, 150, "0000", MARKER);          /* header digits, overwritten */
    rect(0, 0, 0, 0, 0);
    /* Header text sits on the orange bar. */
    img_clear(BG);
    draw_chrome();
    text(60, 45, "- - - : 0 0 0 0", 0xFF101010u);
    draw_readout(0, 0, 0);
    draw_footer(0);
    flush_both(0, FB_H);
    say("touch-ui: screen painted (both buffers)\n");

    /* ---- event loop --------------------------------------------------- */
    int x = 0, y = 0, taps = 0, holding = 0, down = 0;
    static struct input_event ev[64];
    static struct pollfd pfd;

    pfd.fd = tty;
    pfd.events = 0x0001;                     /* POLLIN */
    pfd.revents = 0;

    for (;;) {
        int pr = (int)sc(SYS_poll, (long)&pfd, 1, 500, 0, 0);
        if (pr <= 0) {
            /* Timed out with the finger still down counts towards the hold. */
            if (down && ++holding >= HOLD_POLLS) {
                draw_footer(taps);
                flush_both(FB_H - 160, FB_H);
                power_off("held after 3 taps");
            }
            continue;
        }

        long n = sc(SYS_read, tty, (long)ev, sizeof(ev), 0, 0);
        if (n < (long)sizeof(struct input_event)) continue;

        int count = (int)(n / (long)sizeof(struct input_event));
        for (int i = 0; i < count; i++) {
            if (ev[i].type != EV_ABS) continue;

            if (ev[i].code == ABS_MT_POSITION_X) x = ev[i].value;
            if (ev[i].code == ABS_MT_POSITION_Y) y = ev[i].value;

            if (ev[i].code == ABS_MT_TRACKING_ID) {
                if (ev[i].value >= 0) {
                    /* new contact */
                    down = 1;
                    holding = 0;
                    taps++;
                    say("touch-ui: finger down at ");
                    saynum(x); say(","); saynum(y);
                    say("  tap "); saynum(taps); say("\n");
                } else {
                    down = 0;
                    holding = 0;
                }
            }
        }

        /* Redraw only what changed: the marker, the readout and the footer. */
        rect(40, 200, FB_W - 80, 520, PANEL);          /* erase old marker */
        frame(40, 200, FB_W - 80, 520, 4, ACCENT);
        draw_readout(x, y, taps);
        draw_footer(taps);
        text(60, 45, "X", 0xFF101010u);
        text_num(60 + 4 * (TEXT_W + SCALE), 45, x, 4, 0xFF101010u);
        text(60 + 12 * (TEXT_W + SCALE), 45, "Y", 0xFF101010u);
        text_num(60 + 15 * (TEXT_W + SCALE), 45, y, 4, 0xFF101010u);
        crosshair(x, y, MARKER);
        flush_both(150, FB_H);
    }

    /* unreachable */
    sc(SYS_exit, 0, 0, 0, 0, 0);
}
