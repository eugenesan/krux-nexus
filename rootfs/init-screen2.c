/* init-screen2.c -- the first test whose oracle CANNOT be wrong.
 *
 * Why every previous test was inconclusive, all four of them:
 *
 *   Test N  oracle = "adb appears"  -> INVALID. f_adb is only the kernel-side
 *           endpoint. Nothing enumerates as an adb device until `adbd` (a
 *           userspace daemon) is running. We never launch it. No adb was
 *           possible even on complete success.
 *
 *   Test Q  oracle = "adb appears"  -> INVALID, same reason. Worse, it also
 *           rewrote the gadget, so its failure destroyed the debug channel.
 *
 *   Test O  oracle = "device powers off" -> INVALID. reboot() blocks; proven.
 *   Test P  oracle = "device powers off" -> INVALID, same.
 *
 *   Test R  oracle = "screen changes"  -> probably INVALID. It relied on the
 *           kernel automounting devtmpfs. If it does not, /dev/graphics/fb0
 *           does not exist, open() fails, and the failure message goes to
 *           /dev/console -- which does not exist either, in our minimal
 *           ramdisk. So the process spins silently and the screen never
 *           changes. That is byte-for-byte indistinguishable from
 *           "init never ran".
 *
 * The trap in every one of them: a FAILED action and a NON-RUNNING init look
 * the same, because we have no working output channel.
 *
 * This test removes the dependency. We mount devtmpfs OURSELVES, so fb0
 * cannot be missing for want of a mount. Then we paint. There is nothing left
 * that can fail quietly and nothing that needs a second moving part.
 *
 *   screen turns MAGENTA -> our binary ran as PID 1 and reached the paint.
 *                           (It may still be wrong later, but it RAN.)
 *   screen unchanged     -> it did not run as PID 1 at all.
 *
 * No libc. No stack swap -- the kernel's boot stack is fine and swapping it
 * is a variable we do not need. No USB, no NFC, no reboot, no spin-forever-
 * hoping. Soft-float, matching AT_HWCAP_FP = NO on this device.
 */
typedef unsigned int u32;

#define O_RDWR 2
#define SYS_write     4
#define SYS_open      5
#define SYS_close     6
#define SYS_mkdir     39
#define SYS_mount     21
#define SYS_pwrite64 146

#define FB_H      1920
#define FB_STRIDE 4352          /* measured on this device; NOT width*4 */
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

/* No /dev/console in our ramdisk, so this goes nowhere on a normal boot. Kept
 * anyway: it is free, and if the kernel does provide a console it helps. */
static void bail(const char *why)
{
    puts("screen2: "); puts(why); puts("\n");
    for (;;) for (volatile long k = 0; k < 1000000L; k++) ;
}

static const u32 MAGENTA = 0xFFFF00FFu;   /* BGRA: R=B=255, G=0 */

void _start(void)
{
    /* Mount devtmpfs ourselves. This is the whole point: Test R assumed the
     * kernel would do it, and that assumption is unverified. If it turns out
     * the kernel DOES automount, mount() returns -EBUSY and we carry on. */
    sc(SYS_mkdir, (long)"/dev", 0755, 0, 0, 0, 0);
    long m = sc(SYS_mount, (long)"devtmpfs", (long)"/dev", (long)"devtmpfs", 0, 0, 0);
    puts("screen2: devtmpfs mount returned ");
    puts(m == 0 ? "0 (ok)\n" : (m == -19 ? "-EBUSY (already mounted, fine)\n" : "error\n"));

    int fd = (int)sc(SYS_open, (long)"/dev/graphics/fb0", O_RDWR, 0, 0, 0, 0);
    if (fd < 0) bail("could not open /dev/graphics/fb0 -- no devtmpfs");

    static u32 row[FB_STRIDE / 4];
    for (int i = 0; i < FB_STRIDE / 4; i++) row[i] = MAGENTA;

    /* Buffer 0 only. Buffer 2 does not exist on this device (EFBIG), and a
     * partially-painted screen is harder to read than a fully-painted one. */
    int y;
    for (y = 0; y < FB_H; y++) {
        if (sc(SYS_pwrite64, fd, (long)row, FB_STRIDE,
               (long)y * FB_STRIDE, 0, 0) < 0)
            bail("pwrite64 failed mid-screen");
        if (y % 240 == 0) {
            puts("  painted row "); char b[8]; int n = 0;
            for (int v = y; v && n < 8; v /= 10) b[n++] = '0' + v % 10;
            while (n) sc(SYS_write, 1, (long)&b[--n], 1, 0, 0, 0);
            puts("\n");
        }
    }
    puts("screen2: FULL SCREEN painted magenta\n");
    for (;;) for (volatile long k = 0; k < 1000000L; k++) ;
}
