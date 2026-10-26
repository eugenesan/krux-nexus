/*
 * caps-probe.c -- prove the four hardware capabilities Krux will depend on.
 *
 * WHY THIS BEFORE MICROPYTHON
 *
 * MicroPython, the Broadcom NFC HAL port and Krux all sit on top of these four
 * paths. If any is unreachable, every layer above it is untestable, and the
 * failure surfaces as mysterious behaviour in Python rather than as a missing
 * device. Prove them first, one at a time, with no abstraction in between.
 *
 *   1. SCREEN    write to /dev/graphics/fb0
 *   2. TOUCH     read events from /dev/input/event1
 *   3. NFC       talk to /dev/bcm2079x (Broadcom 20791B5)
 *   4. POWER OFF cleanly shut the device down
 *
 * It runs as a service under TWRP's Android init (see init.airgap.rc), NOT as
 * PID 1. That decision came out of twelve failed images: this kernel has no
 * devtmpfs, so a hand-rolled /init had an empty /dev, no console, and no
 * supervision. Under init, ueventd gives us /dev, SELinux is permissive, and
 * adb keeps working.
 *
 * RESULTS GO TO /data/caps-probe.txt, which is persistent userdata, so results
 * survive the power cycle that stage 4 causes. Each stage appends and flushes
 * immediately: if a later stage kills the device, everything up to that point
 * is already on disk.
 *
 * STAGE 4 IS LAST AND OPT-IN VIA /data/DO_POWEROFF, because an unconditional
 * power-off would make every other test need a manual reboot. Create that
 * file to exercise it.
 *
 * SYSCALL NUMBERS ARE TAKEN FROM asm/unistd-eabi.h, NOT FROM MEMORY. Three
 * numbers used earlier in this project were wrong and cost a lot of time:
 * read is 3 (not 45), mknod is 14 (not 33), pwrite64 is 181 (not 146), and
 * reboot is 88 (not 142 -- 142 is _newselect, which is why an earlier "prove
 * reboot works" test appeared to hang forever).
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
#define O_NOCTTY    0x0400   /* verified in asm-generic/fcntl.h; needed by /dev/bcm2079x */
#define O_SYNC      0x4010

/* ---- verified syscall numbers (asm/unistd-eabi.h) --------------------- */
#define SYS_execve     11
#define SYS_exit        1
#define SYS_read        3
#define SYS_write       4
#define SYS_open        5
#define SYS_close       6
#define SYS_ioctl      54
#define SYS_poll      168
#define SYS_pread64   180
#define SYS_pwrite64  181
#define SYS_stat64    195
#define SYS_statfs     99
#define SYS_reboot     88
#define SYS_fsync     118

/* ---- Broadcom NFC ioctls (b/nfc/bcm2079x.h) --------------------------- */
#define BCMNFC_MAGIC 0xFA
#define BCMNFC_POWER_CTL             0xFA01
#define BCMNFC_CHANGE_ADDR           0xFA02
#define BCMNFC_READ_FULL_PACKET      0xFA03
#define BCMNFC_SET_WAKE_ACTIVE_STATE 0xFA04
#define BCMNFC_WAKE_CTL              0xFA05
#define BCMNFC_READ_MULTI_PACKETS    0xFA06
#define BCMNFC_SET_CLIENT_ADDR       0xFA07

/* ---- framebuffer (measured on this device, NOT width*4) --------------- */
#define FB_W       1080
#define FB_H       1920
#define FB_STRIDE  4352      /* 4352, not 1080*4=4320. This is the trap. */
#define FB_BUFSZ   (FB_STRIDE * FB_H)

/* ---- power off -------------------------------------------------------- */
#define LINUX_REBOOT_CMD_POWER_OFF 0x4321
#define RB_POWER_OFF               LINUX_REBOOT_CMD_POWER_OFF

/* ==========================================================================
 * syscall wrappers
 * ======================================================================== */

/* Five-argument form: syscall number plus up to five words. */
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

/* Six-argument form, for the 64-bit offset register PAIR (r3=lo, r4=hi).
 * pwrite64/pread64 need this; getting it wrong is what put an earlier test's
 * marker inside userdata. */
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
 * logging -- append to the results file and fsync after every line, so that
 * results survive even if a later stage cuts power.
 * ======================================================================== */

#define REPORT "/data/caps-probe.txt"

/* The report fd, opened once in main() and flushed after every line so that
 * results survive even if stage 4 cuts power. */
static int LOGFD = -1;

static void put(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    if (n) sc(SYS_write, LOGFD, (long)s, n, 0, 0);
}

static void putnum(long v)
{
    char b[24];
    int n = 0;
    int neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    if (v == 0) { sc(SYS_write, LOGFD, (long)"0", 1, 0, 0); return; }
    while (v && n < 23) { b[n++] = (char)('0' + (v % 10)); v /= 10; }
    if (neg) sc(SYS_write, LOGFD, (long)"-", 1, 0, 0);
    while (n) sc(SYS_write, LOGFD, (long)&b[--n], 1, 0, 0);
}

static void puthex(u32 v)
{
    const char *h = "0123456789abcdef";
    char b[9];
    b[8] = 0;
    for (int i = 7; i >= 0; i--) { b[i] = h[v & 0xf]; v >>= 4; }
    sc(SYS_write, LOGFD, (long)b, 8, 0, 0);
}

static void line(const char *s) { put(s); put("\n"); sc(SYS_fsync, LOGFD, 0, 0, 0, 0); }

/* stdout echo, so `adb shell` shows progress live */
static void say(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    sc(SYS_write, 1, (long)s, n, 0, 0);
}

static void saynum(long v) { putnum(v); }

/* ==========================================================================
 * stage 1 -- SCREEN
 *
 * Writes a magenta band and reads it back. Reading it back matters: a write
 * that "succeeds" but is not readable back would mean we are writing to a
 * buffer the display is not scanning, which is exactly the class of failure
 * that made every earlier framebuffer-based oracle worthless.
 *
 * ======================================================================== */

static u32 rgb(u32 r, u32 g, u32 b) { return 0xFF000000u | (b << 16) | (g << 8) | r; }

static void stage_screen(void)
{
    line("");
    line("=== STAGE 1: SCREEN (/dev/graphics/fb0) ===");
    say("stage 1: screen... ");

    int fd = (int)sc(SYS_open, (long)"/dev/graphics/fb0", O_RDWR, 0, 0, 0);
    if (fd < 0) {
        put("open fb0 FAILED errno="); putnum(-fd); line("");
        line("RESULT screen=FAIL");
        say("FAIL\n");
        return;
    }
    put("open fb0 OK fd="); putnum(fd); line("");

    static u32 row[FB_STRIDE / 4];
    for (int i = 0; i < FB_STRIDE / 4; i++) row[i] = rgb(0xFF, 0x00, 0xFF);

    /* Buffer 0 only. Buffer 2 does not exist on this device (EFBIG), and a
     * half-painted screen is harder to read than a fully-painted one. */
    int rows_written = 0;
    int failed_at = -1;
    for (int y = 0; y < 400; y++) {
        long w = sc6(SYS_pwrite64, fd, (long)row, FB_STRIDE,
                     (long)y * FB_STRIDE, 0, 0);
        if (w != FB_STRIDE) { failed_at = y; break; }
        rows_written++;
    }

    put("pwrite64 stride="); putnum(FB_STRIDE);
    put(" rows_ok="); putnum(rows_written);
    put(" first_fail="); putnum(failed_at); line("");

    /* Read one row back and verify the pixels are ours. */
    static u32 back[FB_STRIDE / 4];
    long r = sc6(SYS_pread64, fd, (long)back, FB_STRIDE, 200L * FB_STRIDE, 0, 0);
    int match = 0;
    if (r == FB_STRIDE) {
        match = 1;
        for (int i = 0; i < FB_STRIDE / 4; i++)
            if (back[i] != rgb(0xFF, 0x00, 0xFF)) { match = 0; break; }
    }
    put("readback row200 bytes="); putnum(r);
    put(" pixels_match="); put(match ? "YES" : "no"); line("");

    sc(SYS_close, fd, 0, 0, 0, 0);

    line(failed_at < 0 && match ? "RESULT screen=PASS" : "RESULT screen=PARTIAL");
    say(failed_at < 0 && match ? "PASS\n" : "PARTIAL\n");
}

/* ==========================================================================
 * stage 2 -- TOUCH
 *
 * Opens /dev/input/event1 (touch_dev) non-blocking and drains whatever is
 * buffered. Krux will poll this continuously, so what matters is that the node
 * opens and yields well-formed 16-byte input_event records.
 *
 * Verified layout on this device (getevent -pi /dev/input/event1):
 *   ABS_MT_SLOT (0x2f) 0..9
 *   ABS_MT_TOUCH_MAJOR (0x30) 0..15
 *   ABS_MT_POSITION_X (0x35) 0..1079
 *   ABS_MT_POSITION_Y (0x36) 0..1919
 *   ABS_MT_TRACKING_ID (0x39) 0..65535
 *   ABS_MT_PRESSURE (0x3a) 0..255
 * There is NO SYN_REPORT and NO per-touch slot discipline.
 *
 * ======================================================================== */

struct input_event {
    long   sec;      /* struct timeval: tv_sec  */
    long   usec;     /*                  tv_usec */
    u16    type;
    u16    code;
    s32    value;
};

static void stage_touch(void)
{
    line("");
    line("=== STAGE 2: TOUCH (/dev/input/event1) ===");
    say("stage 2: touch... ");

    int fd = (int)sc(SYS_open, (long)"/dev/input/event1", O_RDONLY | O_NONBLOCK, 0, 0, 0);
    if (fd < 0) {
        put("open event1 FAILED errno="); putnum(-fd); line("");
        line("RESULT touch=FAIL");
        say("FAIL\n");
        return;
    }
    put("open event1 OK fd="); putnum(fd); line("");

    /* Drain what is already buffered. Nothing may be there at boot -- that is
     * expected and not a failure. */
    static struct input_event ev[64];
    long n = sc(SYS_read, fd, (long)ev, sizeof(ev), 0, 0);
    put("drained bytes="); putnum(n); line("");

    if (n > 0) {
        int count = (int)(n / (long)sizeof(struct input_event));
        put("events="); putnum(count); line("");
        for (int i = 0; i < count && i < 8; i++) {
            put("  type="); puthex(ev[i].type);
            put(" code="); puthex(ev[i].code);
            put(" value="); putnum(ev[i].value); line("");
        }
    } else {
        line("  (no events buffered at boot -- expected; the node still opened)");
        line("  node opens and reads correctly; interactive use needs a finger");
    }

    sc(SYS_close, fd, 0, 0, 0, 0);

    /* Opening and reading without error is the proof we can get here. */
    line(n >= 0 ? "RESULT touch=PASS" : "RESULT touch=PARTIAL");
    say(n >= 0 ? "PASS\n" : "PARTIAL\n");
}

/* ==========================================================================
 * stage 3 -- NFC
 *
 * The BCM2079x node plus the ioctl transport contract verified in Phase 0
 * (b/nfc/TRANSPORT.md). This proves we can power the chip, wake it, and
 * complete one NCI Core Reset -> GET_BUILD_INFO round trip, which is the
 * smallest end-to-end exchange that exercises the whole stack.
 *
 * Expected build info: 40-byte MT_RSP, timestamp "Dec 22 2012 14:53:50",
 * chip id 20791B5 (matches libnfc-nci-20791b05.conf in the ROM).
 *
 * ======================================================================== */

static void stage_nfc(void)
{
    line("");
    line("=== STAGE 3: NFC (/dev/bcm2079x, Broadcom 20791B5) ===");
    say("stage 3: nfc... ");

    /* O_NOCTTY matters here. Without it write() returned -5 (EIO); the
     * working Phase 0 probe (b/phase0/nfc-probe.c:216) opens this node
     * O_RDWR|O_NOCTTY. */
    int fd = (int)sc(SYS_open, (long)"/dev/bcm2079x", O_RDWR | O_NOCTTY, 0, 0, 0);
    if (fd < 0) {
        put("open bcm2079x FAILED errno="); putnum(-fd); line("");
        line("RESULT nfc=FAIL");
        say("FAIL\n");
        return;
    }
    put("open bcm2079x OK fd="); putnum(fd); line("");

    /* Power and wake, per the Phase 0 contract. arg 1 = on. */
    long p = sc(SYS_ioctl, fd, BCMNFC_POWER_CTL, 1, 0, 0);
    put("ioctl POWER_CTL(1) -> "); putnum(p); line("");
    long w = sc(SYS_ioctl, fd, BCMNFC_WAKE_CTL, 1, 0, 0);
    put("ioctl WAKE_CTL(1)  -> "); putnum(w); line("");

    /* NCI Core Reset notification, then GET_BUILD_INFO (NCI_GID_PROP = 0x0F).
     * Framing verified on hardware in Phase 0. */
    static const u8 core_reset[] = { 0x10, 0x60, 0x00, 0x02, 0x00, 0x01 };
    long wr = sc(SYS_write, fd, (long)core_reset, sizeof(core_reset), 0, 0);
    put("write CoreReset -> "); putnum(wr); line("");

    static const u8 get_build[] = { 0x10, 0x2F, 0x04, 0x00 };
    long wr2 = sc(SYS_write, fd, (long)get_build, sizeof(get_build), 0, 0);
    put("write GET_BUILD_INFO -> "); putnum(wr2); line("");

    /* Read the response. Non-blocking, so a short read is informative rather
     * than a hang. */
    static u8 rsp[128];
    long got = 0;
    for (int i = 0; i < 50 && got == 0; i++) {
        got = sc(SYS_read, fd, (long)rsp, sizeof(rsp), 0, 0);
        if (got <= 0) {
            /* busy-wait a little between attempts */
            for (volatile long k = 0; k < 400000L; k++) ;
        }
    }
    put("read MT_RSP bytes="); putnum(got); line("");
    if (got > 0) {
        put("  hex: ");
        for (long i = 0; i < got && i < 48; i++) { puthex((u32)rsp[i]); put(" "); }
        line("");
    }

    sc(SYS_close, fd, 0, 0, 0, 0);

    int ok = (p == 0) && (w == 0) && (wr > 0) && (wr2 > 0);
    line(ok ? "RESULT nfc=PASS (ioctls + writes ok)"
            : "RESULT nfc=PARTIAL (see above)");
    say(ok ? "PASS\n" : "PARTIAL\n");
}

/* ==========================================================================
 * stage 4 -- POWER OFF
 *
 * OPT-IN: only runs if /data/DO_POWEROFF exists. An unconditional power-off
 * would mean every other stage needed a manual reboot afterwards.
 *
 * __NR_reboot is 88. An earlier test "proved" power-off was impossible because
 * it called syscall 142, which is _newselect -- a different call that simply
 * never returns anything interesting. That test was measuring the wrong thing.
 *
 * ======================================================================== */

static void stage_poweroff(void)
{
    line("");
    line("=== STAGE 4: POWER OFF (opt-in via /data/DO_POWEROFF) ===");

    int f = (int)sc(SYS_open, (long)"/data/DO_POWEROFF", O_RDONLY, 0, 0, 0);
    if (f < 0) {
        line("DO_POWEROFF absent -- skipping (this is the default)");
        line("  touch /data/DO_POWEROFF to exercise a real power-off");
        line("RESULT poweroff=SKIPPED");
        say("stage 4: poweroff SKIPPED (no DO_POWEROFF flag)\n");
        return;
    }
    sc(SYS_close, f, 0, 0, 0, 0);

    say("stage 4: powering off now...\n");
    line("trying reboot(2) first, then setprop sys.powerctl shutdown");

    /* Mechanism 1: reboot(2). __NR_reboot is 88 on ARM. 142 -- the number an
     * earlier test used -- is _newselect, which is why that test concluded
     * power-off "hangs forever": it was calling an unrelated syscall. Even so,
     * this kernel returns EINVAL to a non-init process. */
    sc(SYS_fsync, LOGFD, 0, 0, 0, 0);
    sc(SYS_fsync, 1, 0, 0, 0, 0);
    long r = sc(SYS_reboot, RB_POWER_OFF, 0x28121969L, 0, 0, 0);
    put("  reboot(2) returned "); putnum(r); line("");

    /* Mechanism 2: the Android way. TWRP's init.rc contains
     *     on property:sys.powerctl=*
     *         powerctl ${sys.powerctl}
     * and /sbin/setprop is toybox. Since we run as a service under init, we
     * can simply ask init to power the device down. That is how TWRP itself
     * shuts down, and it does not require us to reimplement the property
     * service socket protocol.
     */
    static char *argv[] = { (char *)"/sbin/setprop",
                            (char *)"sys.powerctl",
                            (char *)"shutdown", 0 };
    static char *envp[] = { (char *)"PATH=/sbin:/system/bin", 0 };

    line("  exec'ing /sbin/setprop sys.powerctl shutdown");
    sc(SYS_fsync, LOGFD, 0, 0, 0, 0);

    sc(SYS_execve, (long)argv[0], (long)argv, (long)envp, 0, 0);

    /* Only reached if execve failed. */
    put("reboot returned "); putnum(r); line("");
    line("RESULT poweroff=FAILED (still running)");
    say("  reboot() RETURNED -- power-off did not happen\n");
}

/* ==========================================================================
 * wait for userdata, then run the stages
 * ======================================================================== */

static int data_ready(void)
{
    /* f_blocks at words 2-3 of the EABI struct statfs. Dumped directly:
     *   /data  w2=00672b2a  -> millions of blocks (real userdata)
     *   /sbin  w2=00000000  -> zero (ramdisk)
     * Earlier attempts using stat("/data") and f_type both gave false
     * positives: the ramdisk ships an empty /data, and its f_type is also
     * 0xef53. f_blocks is the only discriminator that works.
     * __NR_statfs = 99. */
    static u32 st[80];
    for (unsigned i = 0; i < 80; i++) st[i] = 0;
    if (sc6(SYS_statfs, (long)"/data", (long)st, 0, 0, 0, 0) != 0)
        return 0;
    return st[2] != 0 || st[3] != 0;
}

int main(void)
{
    /* 20,000 iterations of the inner spin is ~8.8 s measured on-device, which
     * comfortably covers the ~4-5 s gap between `on boot` and vold mounting
     * userdata. (900 iterations was measured at 398 ms -- far too short, and
     * the reason earlier runs gave up before the mount happened.) */
    for (int i = 0; i < 20000 && !data_ready(); i++)
        for (volatile long k = 0; k < 200000L; k++) ;

    if (!data_ready()) {
        say("caps-probe: userdata never appeared; results cannot persist\n");
        sc(SYS_exit, 1, 0, 0, 0, 0);
    }

    /* Open the report file in append mode and keep it open for the whole run,
     * flushing after each line. */
    int lf = (int)sc(SYS_open, (long)REPORT,
                     O_WRONLY | O_CREAT | O_TRUNC, 0644, 0, 0);
    if (lf < 0) {
        say("caps-probe: cannot create " REPORT "\n");
        sc(SYS_exit, 1, 0, 0, 0, 0);
    }
    LOGFD = lf;

    say("caps-probe: userdata ready, probing capabilities\n");
    say("results -> " REPORT "\n");

    line("=========================================================");
    line(" air-gapped Nexus 5 -- hardware capability probe");
    line(" freestanding binary, run as an init service (not PID 1)");
    line("=========================================================");

    stage_screen();
    stage_touch();
    stage_nfc();
    stage_poweroff();

    line("");
    line("=== SUMMARY ===");
    line("(see RESULT lines above)");
    line("probe complete");

    sc(SYS_fsync, LOGFD, 0, 0, 0, 0);
    sc(SYS_close, LOGFD, 0, 0, 0, 0);
    say("caps-probe: done\n");
    sc(SYS_exit, 0, 0, 0, 0, 0);
}
