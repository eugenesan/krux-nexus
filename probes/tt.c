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

/* ---- MSMFB display commit, from include/linux/msm_mdp.h --------------- */
struct fb_var_screeninfo {
    u32 xres, yres;
    u32 xres_virtual, yres_virtual;
    u32 xoffset, yoffset;
    u32 bits_per_pixel;
    u32 grayscale, nonstd, activate, height, width, accel_flags;
    u32 pixclock, left_margin, upper_margin, lower_margin, right_margin;
    u32 hsync_len, vsync_len, sync, vmode, rotate, colorspace, nonstd_param;
    u16 activate_s, rotate_2;
    u16 reserved[4];
};
struct mdp_display_commit {
    u32 flags;
    u32 wait_for_finish;
    struct fb_var_screeninfo var;
};

/* From include/linux/msm_mdp.h:  _IOW(MSMFB_IOCTL_MAGIC, 164, struct mdp_display_commit)
 * where MSMFB_IOCTL_MAGIC == 'm' == 0x6D. _IOW encodes the SIZE OF THE THIRD
 * ARGUMENT, so if either struct below changed size the ioctl number would
 * change and MSMFB_DISPLAY_COMMIT would silently become the wrong ioctl --
 * which is precisely the silent-wrong-constant failure this project keeps
 * hitting.
 *
 * These tripwires turn that into a BUILD FAILURE instead. gcc rejects a
 * negative array bound, so changing either struct breaks the build here.
 * Measured with the cross compiler: fb_var_screeninfo = 0xA0 = 160,
 * mdp_display_commit = 0xA8 = 168, giving
 *   (1<<30) | (168<<16) | ('m'<<8) | 164 = 0x40A86DA4
 */
#define MSMFB_DISPLAY_COMMIT 0x40A86DA4u

typedef char tripwire_fb_var_size[
    (sizeof(struct fb_var_screeninfo) == 160) ? 1 : -1];
typedef char tripwire_commit_size[
    (sizeof(struct mdp_display_commit) == 167) ? 1 : -1];
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
static void barcode(long v)
{
    if (v < 0) v = -v;
    if (v > 64) v = 64;

    const long slot = FB_STRIDE / 64;       /* 68 pixels */
    const long words_per_row = FB_STRIDE / 4;

    for (long y = 0; y < FB_H; y++) {
        u32 *row = &g_base[y * words_per_row];
        for (long i = 0; i < 64; i++) {
            u32 c = (i < v) ? C_WHITE : C_BLACK;
            for (long x = 0; x < slot; x++) row[i * slot + x] = c;
        }
    }
    /* mirror into plane 2 so a commit can never show an unfilled plane */
    for (long i = 0; i < (long)g_plane_words; i++)
        g_base[g_plane_words + i] = g_base[i];
    screen_commit();
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

static void nfc_test(void)
{
    int fd = (int)sc(SYS_open, (long)"/dev/bcm2079x", O_RDWR | O_NOCTTY, 0, 0, 0);
    if (fd < 0) { line("  bcm2079x open FAILED errno=", -fd); return; }
    say("  bcm2079x opened\n");

    line("  ioctl POWER_CTL -> ", sc(SYS_ioctl, fd, BCMNFC_POWER_CTL, 1, 0, 0));
    line("  ioctl WAKE_CTL  -> ", sc(SYS_ioctl, fd, BCMNFC_WAKE_CTL, 1, 0, 0));

    /* Core Reset, then GET_BUILD_INFO. The working Phase 0 probe
     * (b/phase0/nfc-probe.c) sends exactly these byte sequences. */
    static const u8 core_reset[] = { 0x10, 0x60, 0x00, 0x02, 0x00, 0x01 };
    static const u8 get_build[]  = { 0x10, 0x2F, 0x04, 0x00 };
    line("  write CoreReset     -> ", sc(SYS_write, fd, (long)core_reset,
                                          sizeof(core_reset), 0, 0));
    line("  write GET_BUILD_INFO -> ", sc(SYS_write, fd, (long)get_build,
                                          sizeof(get_build), 0, 0));

    static u8 rsp[64];
    long got = 0;
    for (int i = 0; i < 50 && got <= 0; i++) {
        got = sc(SYS_read, fd, (long)rsp, sizeof(rsp), 0, 0);
        if (got <= 0) delay(400000L);
    }
    line("  read MT_RSP bytes=", got);
    sc(SYS_close, fd, 0, 0, 0, 0);
}

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

    /* ---- 4. NFC ---- */
    say("  stage 4: NFC\n");
    nfc_test();

    /* ---- 5. green ---- */
    paint(C_GREEN);
    say("  stage 5 GREEN committed\n");

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

    barcode(rc);
    say("  barcode drawn: count the bars = |rc|\n");
    for (;;) delay(1000000L);
}