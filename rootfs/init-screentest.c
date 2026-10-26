/* init-screentest.c -- an oracle you can see without any host tooling.
 *
 * Test Q used "did adb enumerate" as its signal. That needs the USB gadget to
 * cooperate, and it conflates "init never ran" with "the gadget write failed".
 * The framebuffer needs no cooperation at all: either the screen changes or it
 * does not, and a human can see which.
 *
 * Configured under CONFIG_DEVTMPFS_MOUNT (normal for Android kernels) the
 * kernel mounts devtmpfs on /dev BEFORE running /init, so /dev/graphics/fb0
 * exists with no mount() call of our own. That is the assumption this test
 * exists to check, and it is cheap to check.
 *
 * Deliberately absent: mount, USB gadget, reboot, NFC, libc, stack swap,
 * any loop other than the final spin. Nothing that can block.
 *
 *   screen turns MAGENTA  -> our binary ran as PID 1. Full stop.
 *   screen stays on the bootloader logo -> it did not run at all.
 */
typedef unsigned int u32;

#define O_RDWR 2
#define SYS_write     4
#define SYS_open      5
#define SYS_close     6
#define SYS_pwrite64 146

#define FB_H      1920
#define FB_STRIDE 4352           /* NOT width*4. Measured on this device. */
#define FB_BUFSZ  (FB_STRIDE * FB_H)

static inline long sc(long n, long a, long b, long c, long d, long e, long f)
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

static void puts(const char *s)
{
    int n = 0; while (s[n]) n++;
    sc(SYS_write, 1, (long)s, n, 0, 0, 0);
}

static const u32 MAGENTA = 0xFFFF00FFu;   /* BGRA: R=B=255, G=0 */

void _start(void)
{
    puts("screentest: opening fb0\n");
    int fd = (int)sc(SYS_open, (long)"/dev/graphics/fb0", O_RDWR, 0, 0, 0, 0);
    if (fd < 0) {
        puts("screentest: NO fb0 -- devtmpfs was not automounted\n");
        for (;;) for (volatile long k = 0; k < 1000000L; k++) ;
    }

    /* One buffer worth of rows, 200 at a time, so a failure part-way through
     * still leaves visible evidence rather than nothing. */
    static u32 row[FB_STRIDE / 4];
    for (int i = 0; i < FB_STRIDE / 4; i++) row[i] = MAGENTA;

    int buf = 0;
    for (int band = 0; band < FB_H / 200; band++) {
        int y0 = band * 200, y1 = y0 + 200;
        for (int y = y0; y < y1; y++)
            sc(SYS_pwrite64, fd, (long)row, FB_STRIDE,
               (long)buf * FB_BUFSZ + (long)y * FB_STRIDE, 0, 0);
        puts("  band ");
        char msg[24]; int n = 0;
        for (int v = band; v && n < 20; v /= 10) msg[n++] = '0' + v % 10;
        while (n) sc(SYS_write, 1, (long)&msg[--n], 1, 0, 0, 0);
        puts(" done\n");
    }
    puts("screentest: screen should be MAGENTA\n");
    for (;;) for (volatile long k = 0; k < 1000000L; k++) ;
}
