/*
 * touch-off.c -- fill the screen with one colour, power off when touched.
 *
 * WHAT THIS PROVES, VISUALLY
 *
 * caps-probe.c showed that /dev/graphics/fb0 accepts writes and that the data
 * reads back byte-identical. That is a hardware fact, not a demonstration --
 * nobody confirmed a human can SEE the output. If the panel were scanning a
 * buffer we do not write, or TWRP repainted over us a frame later, every
 * "PASS" would still be true and the screen would still show TWRP.
 *
 * So: fill the entire panel with magenta, which is not a colour TWRP uses, and
 * then wait. If the phone turns magenta, the display path is proven by eye.
 * Touch the screen and the device powers off, which proves the touch path and
 * the power path in the same interaction.
 *
 * WHY WE DO *NOT* STOP TWRP
 *
 * The first version's .rc did `stop recovery` before starting this, on the
 * assumption that TWRP would repaint over us. On hardware that was a
 * disaster: the screen stayed on the BOOTLOADER logo and not one pixel ever
 * changed.
 *
 * Cause: `on boot` fires long before TWRP's recovery has powered the panel on
 * and programmed the MDP/DSI pipeline. Killing it at that point means the
 * display is never handed over from the bootloader at all. Our writes went
 * into a framebuffer that no display hardware was scanning.
 *
 * This is precisely the failure the readback check could NOT see: every byte
 * we wrote was correct and verifiable in memory, and none of it was visible.
 * "Reads back correctly" is not "is displayed".
 *
 * So now TWRP is left alone, we wait (init.touchoff.rc sleeps 8 s), and then
 * we take the framebuffer. Recovery will not redraw over us because it has
 * finished its startup and is idle waiting for input.
 *
 * STRIDE IS 4352, NOT 1080*4 = 4320
 *
 * That 32-byte-per-row difference is the most likely way to get a skewed or
 * entirely invisible image. Both buffers are painted because we do not know
 * which one the panel is scanning, and painting both is free. Buffer 2 does not
 * exist on this device (EFBIG).
 *
 * POWER-OFF USES THE ANDROID MECHANISM
 *
 * reboot(2) returns EINVAL to a non-init process on this CAF kernel, so we ask
 * init instead: TWRP's init.rc has `on property:sys.powerctl=* / powerctl
 * ${sys.powerctl}`, and /sbin/setprop is toybox. We run as a service under
 * init, so that is a one-line exec.
 *
 * SYSCALL NUMBERS are taken from asm/unistd-eabi.h and were checked in the
 * disassembly before shipping. Earlier tests in this project used read=45,
 * mknod=33, pwrite64=146, statfs=140 and reboot=142; all five were wrong.
 * reboot is 88. 142 is _newselect.
 */

typedef unsigned short u16;
typedef unsigned int  u32;
typedef int           s32;

/* ---- flags ------------------------------------------------------------- */
#define O_RDWR      0x0002
#define O_RDONLY    0x0000
#define O_NONBLOCK  0x0800

/* ---- syscall numbers (verified) ---------------------------------------- */
#define SYS_exit      1
#define SYS_read      3
#define SYS_write     4
#define SYS_open      5
#define SYS_close     6
#define SYS_execve   11
#define SYS_poll    168
#define SYS_ioctl   54
#define SYS_pread64 180
#define SYS_pwrite64 181
#define SYS_reboot   88

/* ---- framebuffer ------------------------------------------------------- */
#define FB_W        1080
#define FB_H        1920
#define FB_STRIDE   4352                 /* NOT width*4 */
#define FB_ROWU     (FB_STRIDE / 4)      /* 1088 */
#define FB_BUFSZ    (FB_STRIDE * FB_H)   /* 8,355,840 */
#define FB_NBUF     2                     /* buffer 2 does not exist: EFBIG */

/* magenta, BGRA */
#define MAGENTA 0xFFFF00FFu
/* a second colour, used only for the instant before power-off */
#define WHITE   0xFFFFFFFFu
/* green, BGRA -- plane 1 marker for the two-plane diagnostic */
#define GREEN   0xFF00FF00u

/* ---- touch codes (getevent -pi /dev/input/event1) ---------------------- */
#define EV_ABS              0x03
#define ABS_MT_POSITION_X   0x35   /* 0..1079 */
#define ABS_MT_POSITION_Y   0x36   /* 0..1919 */
#define ABS_MT_TRACKING_ID  0x39   /* 0..65535, -1 = released */

/* ==========================================================================
 * syscall wrappers
 * ======================================================================== */

/* Five-argument form. */
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

/* Six-argument form: a 64-bit offset lives in the register PAIR (r3=lo, r4=hi).
 * pwrite64 needs this. Passing the offset in a single register is what put an
 * earlier test's marker inside userdata instead of past the last partition. */
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
 * console echo -- stdout is adb, not the panel
 * ======================================================================== */

static void say(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    sc(SYS_write, 1, (long)s, n, 0, 0);
}

static void sayhex(u32 v)
{
    const char *h = "0123456789abcdef";
    char b[9];
    b[8] = 0;
    for (int i = 7; i >= 0; i--) { b[i] = h[v & 0xf]; v >>= 4; }
    sc(SYS_write, 1, (long)b, 8, 0, 0);
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
 * screen
 * ======================================================================== */

static u32 g_row[FB_ROWU];

static int fb = -1;

/* Paint every row of one buffer with one colour. */
static int fill_buffer(int buf, u32 colour)
{
    for (int i = 0; i < FB_ROWU; i++) g_row[i] = colour;

    for (int y = 0; y < FB_H; y++) {
        long off = (long)buf * FB_BUFSZ + (long)y * FB_STRIDE;
        long w = sc6(SYS_pwrite64, fb, (long)g_row, FB_STRIDE, off, 0, 0);
        if (w != FB_STRIDE) {
            say("  fill_buffer: short write at row ");
            saynum(y);
            say(" (");
            saynum(w);
            say(" bytes)\n");
            return 0;
        }
    }
    return 1;
}

/* Read one row back so we can state the write landed, not just returned 0. */
static int verify(u32 colour)
{
    static u32 back[FB_ROWU];
    long r = sc6(SYS_pread64, fb, (long)back, FB_STRIDE,
                 (long)(FB_H / 2) * FB_STRIDE, 0, 0);
    if (r != FB_STRIDE) { say("  verify: short read\n"); return 0; }
    for (int i = 0; i < FB_ROWU; i++)
        if (back[i] != colour) { say("  verify: pixel mismatch\n"); return 0; }
    return 1;
}

/* ==========================================================================
 * power off
 * ======================================================================== */

static void power_off(void)
{
    say("  powering off...\n");

    /* Try the direct route first. This kernel returns EINVAL to a non-init
     * process, but it costs nothing to ask. */
    sc(SYS_reboot, 0x4321L, 0x28121969L, 0, 0, 0);

    /* The route that works: ask init, which owns the powerctl handler. */
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
    say("touch-off: start\n");

    fb = (int)sc(SYS_open, (long)"/dev/graphics/fb0", O_RDWR, 0, 0, 0);
    if (fb < 0) { say("touch-off: cannot open fb0\n"); sc(SYS_exit, 1, 0, 0, 0, 0); }
    say("touch-off: fb0 open\n");

    int tty = (int)sc(SYS_open, (long)"/dev/input/event1",
                      O_RDONLY | O_NONBLOCK, 0, 0, 0);
    if (tty < 0) { say("touch-off: cannot open event1\n"); sc(SYS_exit, 1, 0, 0, 0, 0); }
    say("touch-off: event1 open\n");

    /* TWO-PLANE DIAGNOSTIC.
     *
     * The panel lights up but shows no magenta, even though every row of fb0
     * was written and read back correctly. Write plane 0 MAGENTA and plane 1
     * GREEN, then the colour on screen identifies which plane the display
     * controller is actually scanning:
     *
     *   green on screen  -> it scans plane 1; our offset convention is wrong
     *   magenta          -> it scans plane 0 but is not showing fb memory
     *   neither          -> the mdp is not scanning this fb at all
     *   bootloader logo  -> the panel is still owned by the bootloader
     */
    int ok = fill_buffer(0, MAGENTA);
    ok &= fill_buffer(1, GREEN);
    int vr = verify(MAGENTA);
    /* Read the driver's own idea of the geometry. If our hardcoded constants
     * are wrong, this is where we find out -- and it distinguishes "wrong
     * stride" from "display not scanning our buffer". */
    {
        static u32 fi[64];
        long r = sc(SYS_ioctl, fb, 0x4600, (long)fi, 0, 0);   /* FBIOGET_FSCREENINFO */
        if (r == 0) {
            say("touch-off: FBIOGET_FSCREENINFO id=");
            for (int i = 0; i < 4; i++) { sayhex(fi[i]); say(" "); }
            say("\n");
        } else {
            say("touch-off: FBIOGET_FSCREENINFO failed (");
            saynum(r);
            say(")\n");
        }
    }

    {
        static u32 b0[FB_ROWU], b1[FB_ROWU];
        long r0 = sc6(SYS_pread64, fb, (long)b0, FB_STRIDE,
                      (long)(FB_H / 2) * FB_STRIDE, 0, 0);
        long r1 = sc6(SYS_pread64, fb, (long)b1, FB_STRIDE,
                      (long)FB_BUFSZ + (long)(FB_H / 2) * FB_STRIDE, 0, 0);
        say("touch-off: plane0 mid-row = "); sayhex(r0 == FB_STRIDE ? b0[100] : 0);
        say("  plane1 mid-row = ");     sayhex(r1 == FB_STRIDE ? b1[100] : 0);
        say("  (want a1a0 for plane0, 00ff00 for plane1)\n");
    }

    say("touch-off: plane0=MAGENTA plane1=GREEN");
    say(ok ? " (all rows written)" : " (SHORT WRITE)");
    say(vr ? ", readback verified\n" : ", readback FAILED\n");

    if (!ok) {
        say("touch-off: not continuing, the screen may not be showing this\n");
        sc(SYS_exit, 1, 0, 0, 0, 0);
    }

    say("touch-off: TOUCH THE SCREEN TO POWER OFF\n");

    static struct input_event ev[64];
    static struct pollfd pfd;
    pfd.fd = tty;
    pfd.events = 0x0001;          /* POLLIN */
    pfd.revents = 0;

    int x = 0, y = 0;

    /* Require a real, complete gesture before acting.
     *
     * BUG THIS FIXES: the first hardware run flashed the screen (proving the
     * framebuffer write finally became visible) and then powered off with
     * nobody touching it. The old code fired on ANY TRACKING_ID >= 0, and this
     * driver emits an initial contact on open, so it triggered instantly.
     *
     * A genuine tap is a full sequence, and we require all of it:
     *     TRACKING_ID >= 0   (finger down)
     *     POSITION_X and POSITION_Y both seen  (a real contact has coords)
     *     ...then TRACKING_ID == -1 (finger lifted)
     * Requiring the release also means a finger resting on the screen at boot
     * cannot power the device off.
     */
    int saw_down = 0, saw_x = 0, saw_y = 0, armed = 0;

    /* Settle first: drain whatever the driver queued when we opened the node,
     * so a stale contact cannot arm us. */
    {
        static struct input_event junk[64];
        sc(SYS_read, tty, (long)junk, sizeof(junk), 0, 0);
    }

    for (;;) {
        int pr = (int)sc(SYS_poll, (long)&pfd, 1, 1000, 0, 0);
        if (pr <= 0) {
            /* Arm after the first idle second: this guarantees the device has
             * been up and on screen for a while before a tap can do anything. */
            armed = 1;
            continue;
        }

        long n = sc(SYS_read, tty, (long)ev, sizeof(ev), 0, 0);
        if (n < (long)sizeof(struct input_event)) continue;

        int count = (int)(n / (long)sizeof(struct input_event));
        for (int i = 0; i < count; i++) {
            if (ev[i].type != EV_ABS) continue;
            if (ev[i].code == ABS_MT_POSITION_X) x = ev[i].value;
            if (ev[i].code == ABS_MT_POSITION_Y) y = ev[i].value;

            /* value >= 0 means a new contact; -1 means lifted. */
            if (ev[i].code == ABS_MT_POSITION_X) { x = ev[i].value; saw_x = 1; }
            if (ev[i].code == ABS_MT_POSITION_Y) { y = ev[i].value; saw_y = 1; }

            if (ev[i].code == ABS_MT_TRACKING_ID) {
                if (ev[i].value >= 0) {
                    saw_down = 1;
                    saw_x = saw_y = 0;
                } else {
                    /* finger lifted: this counts as a tap only if we saw a
                     * down AND both coordinates */
                    if (saw_down && saw_x && saw_y && armed) {
                        say("touch-off: TAP at x=");
                        saynum(x);
                        say(" y=");
                        saynum(y);
                        say("\n");

                        /* WRITE-ONLY DIAGNOSTIC: deliberately no power-off.
                         * A stray touch must not cut the run short. */
                        say("touch-off: (tap seen, NOT powering off -- diagnostic build)\n");
                    }
                    saw_down = saw_x = saw_y = 0;
                }
            }
        }
    }

    sc(SYS_exit, 0, 0, 0, 0, 0);
}
