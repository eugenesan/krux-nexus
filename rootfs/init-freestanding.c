/*
 * init-freestanding.c -- Phase 2 init for the stock hammerhead kernel.
 *
 * Rewritten rather than patched, after two patch attempts silently dropped
 * code. Every stage is announced on fd 1 AND is something you can observe from
 * outside, because a hang and a crash look identical: both leave the bootloader
 * logo up with no adb.
 *
 * ORDERING IS THE POINT. USB comes FIRST, before the framebuffer, before NFC.
 * Anything after it may hang or fault; adb will already be up, so the failure
 * becomes debuggable instead of a guessing game.
 *
 * Freestanding by design:
 *   - no libc, so no loader, no PIE, no bionic/glibc ABI question
 *   - -mfloat-abi=soft -marm, and verified to emit ZERO VFP/NEON instructions,
 *     because the kernel reports AT_HWCAP_FP = NO
 *   - ET_EXEC, like every binary that has ever worked on this kernel
 *
 * Build:
 *   arm-linux-gnueabihf-gcc -O2 -static -nostdlib -ffreestanding \
 *     -fno-stack-protector -fno-asynchronous-unwind-tables \
 *     -mfloat-abi=soft -marm -march=armv7-a -mtune=cortex-a9 \
 *     -o init init-freestanding.c
 */

typedef unsigned int  u32;
typedef long           ssize_t;

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2
#define O_CREAT  0x40
#define O_TRUNC  0x200
#define O_NOCTTY 0x100

#define SYS_exit      1
#define SYS_write     4
#define SYS_open      5
#define SYS_close     6
#define SYS_mkdir     39
#define SYS_mount    21
#define SYS_ioctl    54
#define SYS_pwrite64 146

#define FB_H      1920
#define FB_STRIDE 4352          /* NOT width*4 = 4320 -- this is the trap */
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

/* 5-argument convenience wrapper: mount/mkdir/ioctl take at most 5. */
static inline long sc5(long n, long a, long b, long c, long d)
{
    return sc(n, a, b, c, d, 0, 0);
}

#define SYSCALL_write(f, b, l) sc(SYS_write, (long)(f), (long)(b), (long)(l), 0, 0, 0)

static void puts(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    SYSCALL_write(1, s, n);
}

static void say(const char *a, const char *b)
{
    puts(a);
    puts(b);
    puts("\n");
}

/* No floating point in this file, deliberately. */
static u32 rgb(u32 r, u32 g, u32 b)
{ return 0xFF000000u | (b << 16) | (g << 8) | r; }

static void paint(int fd, int buf, int y0, int y1, u32 colour)
{
    static u32 row[FB_STRIDE / 4];
    for (int i = 0; i < FB_STRIDE / 4; i++) row[i] = colour;
    long base = (long)buf * FB_BUFSZ;
    for (int y = y0; y < y1 && y < FB_H; y++)
        if (sc5(SYS_pwrite64, fd, (long)row, FB_STRIDE,
                base + (long)y * FB_STRIDE) < 0)
            return;
}

void _start(void)
{
    /* sp at the TOP of the region, 16-byte aligned, 12 bytes AAPCS-reserved.
     * Pointing it at the low end grows the stack out of .bss -> SIGSEGV.
     * sp must be declared clobbered: the compiler is otherwise free to use
     * sp-relative addressing that this just invalidated. */
    static char stack[16384] __attribute__((aligned(16)));
    long sp = (long)(stack + sizeof(stack));
    sp &= ~15L; sp -= 16;
    __asm__ volatile("mov sp, %0" :: "r"(sp) : "memory");

    puts("\n=== init: stage 0, alive ===\n");

#ifndef DRY_RUN
    /* ---- mount what we need -------------------------------------------
     * DESTRUCTIVE inside a live system: mounting devtmpfs over /dev replaces
     * the running system's device nodes, which takes adb with it. Compile with
     * -DDRY_RUN to test this binary safely inside Android/TWRP. */
    sc5(SYS_mkdir, (long)"/proc", 0755, 0, 0);
    sc5(SYS_mount, (long)"proc", (long)"/proc", (long)"proc", 0);
    sc5(SYS_mkdir, (long)"/sys", 0755, 0, 0);
    sc5(SYS_mount, (long)"sysfs", (long)"/sys", (long)"sysfs", 0);
    sc5(SYS_mount, (long)"devtmpfs", (long)"/dev", (long)"devtmpfs", 0);
    say("stage 1:", "mounted /proc /sys /dev");
#else
    say("stage 1:", "SKIPPED (DRY_RUN)");
#endif

#ifndef DRY_RUN
    /* ---- USB FIRST ----------------------------------------------------- */
    static const char *W[] = {
        "/sys/class/android_usb/android0/enable",    "0",
        "/sys/class/android_usb/android0/idVendor",  "0x18d1",
        "/sys/class/android_usb/android0/idProduct", "0x4ee2",
        "/sys/class/android_usb/android0/functions", "adb acm",
        "/sys/class/android_usb/android0/enable",    "1", 0, 0
    };
    for (int i = 0; W[i]; i += 2) {
        int f = (int)sc(SYS_open, (long)W[i], O_WRONLY | O_CREAT | O_TRUNC, 0644, 0, 0, 0);
        if (f < 0) { say("  usb FAILED:", W[i]); continue; }
        int n = 0; while (W[i + 1][n]) n++;
        SYSCALL_write(f, W[i + 1], n);
        sc5(SYS_close, f, 0, 0, 0);
    }
    say("stage 2:", "USB gadget set to 'adb acm' -- CHECK THE HOST NOW");
#else
    say("stage 2:", "SKIPPED (DRY_RUN)");
#endif

    /* ---- framebuffer: small band, buffers 0 and 1 only ----------------- */
    int fb = (int)sc(SYS_open, (long)"/dev/graphics/fb0", O_RDWR, 0, 0, 0, 0);
    if (fb < 0) {
        say("stage 3:", "no /dev/graphics/fb0");
    } else {
        paint(fb, 0, 0, 400, rgb(0xFF, 0xFF, 0xFF));
        paint(fb, 1, 0, 400, rgb(0xFF, 0xFF, 0xFF));
        say("stage 3:", "painted 400 rows into buffers 0 and 1");
    }

    /* ---- NFC ----------------------------------------------------------- */
    int n = (int)sc(SYS_open, (long)"/dev/bcm2079x", O_RDWR | O_NOCTTY, 0, 0, 0, 0);
    if (n < 0) {
        say("stage 4:", "no /dev/bcm2079x");
    } else {
        static const long R[] = { 0xFA01, 0xFA05, 0xFA07, 0xFA04 };
        static const char *M[] = { "POWER_CTL", "WAKE_CTL", "SET_CLIENT_ADDR",
                                   "SET_WAKE_ACTIVE_STATE" };
        for (int i = 0; i < 4; i++) {
            if (sc5(SYS_ioctl, n, R[i], 1, 0) == 0) say("  ioctl OK:", M[i]);
            else say("  ioctl FAILED:", M[i]);
        }
        sc5(SYS_close, n, 0, 0, 0);
    }

    /* ---- heartbeat: repaint a moving band so liveness is visible ------- */
    for (int i = 0; i < 600; i++) {
        if (fb >= 0) {
            paint(fb, 0, 0, FB_H, rgb(0, 0, 0));
            paint(fb, 0, (i % 16) * 120, (i % 16) * 120 + 120,
                  rgb(0x40 + (u32)(i % 8) * 28, 0x80, 0xFF));
        }
        for (volatile long k = 0; k < 20000000L; k++) ;
    }

    /* Returning from init is a panic and we are PID 1, so never return. */
    puts("init: heartbeat done, sleeping forever\n");
    for (;;) for (volatile long k = 0; k < 10000000L; k++) ;
}
