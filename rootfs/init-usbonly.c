/* init-usbonly.c -- the first test whose oracle is NOT "hang vs. work".
 *
 * Why this exists: every previous test used hang-or-adb as its signal, and a
 * hang conflates three causes -- never ran, ran and hung, ran and got stuck in
 * a syscall. Test O and Test P were both explained by `reboot()` blocking,
 * which we just proved in TWRP. Neither told us anything about PID 1.
 *
 * So: no mounts, no framebuffer, no NFC, no reboot, no libc, no stack swap.
 * Nothing that can block. Five sysfs writes, then spin forever.
 *
 * The kernel mounts sysfs and (with CONFIG_DEVTMPFS_MOUNT) devtmpfs itself
 * before running /init, so /sys/class/android_usb is already there.
 *
 *   adb enumerates  -> our binary RAN as PID 1. The bug is in whatever came
 *                      after it in the earlier tests (mount, fb, NFC).
 *   no adb, hang    -> our binary does not run as PID 1 AT ALL, and none of
 *                      the C code is relevant until that is explained.
 *
 * Those two are distinguishable from outside, which is the whole point.
 */
#define SYS_write     4
#define SYS_open      5
#define SYS_close     6

#define O_WRONLY 1
#define O_CREAT  0x40
#define O_TRUNC  0x200

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

void _start(void)
{
    static const char *W[] = {
        "/sys/class/android_usb/android0/enable",    "0",
        "/sys/class/android_usb/android0/idVendor",  "0x18d1",
        "/sys/class/android_usb/android0/idProduct", "0x4ee2",
        "/sys/class/android_usb/android0/functions", "adb acm",
        "/sys/class/android_usb/android0/enable",    "1", 0, 0
    };
    for (int i = 0; W[i]; i += 2) {
        int f = (int)sc(SYS_open, (long)W[i], O_WRONLY | O_CREAT | O_TRUNC, 0644, 0, 0, 0);
        puts(f < 0 ? "usb open FAILED: " : "usb wrote: ");
        puts(W[i]);
        puts(f < 0 ? "\n" : " ok\n");
        if (f < 0) continue;
        int n = 0; while (W[i + 1][n]) n++;
        sc(SYS_write, f, (long)W[i + 1], n, 0, 0, 0);
        sc(SYS_close, f, 0, 0, 0, 0, 0);
    }
    puts("usb init done; spinning\n");
    for (;;) for (volatile long k = 0; k < 1000000L; k++) ;
}
