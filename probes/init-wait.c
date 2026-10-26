/* init-airgap.c -- our userspace, running as a normal process under TWRP's
 * Android init instead of replacing it.
 *
 * WHY THIS IS THE RIGHT ARCHITECTURE (and why it took 12 failed images to see)
 *
 * Everything that went wrong before came from trying to BE PID 1:
 *
 *   - /dev was empty, because there is no devtmpfs on this kernel and our
 *     initramfs had no mknod'd nodes. TWRP's ueventd does that job for us.
 *   - mknod from an unlabelled process returns EINVAL (SELinux/devcgroup), so
 *     we could not even populate /dev ourselves.
 *   - SELinux was enforcing with no policy context for a bare binary.
 *   - We had no /dev/console, so every failure was silent.
 *
 * None of that is our problem any more. TWRP's init:
 *   - populates /dev via ueventd (so /dev/graphics/fb0, /dev/bcm2079x and
 *     /dev/block/* all exist),
 *   - has already mounted pstore, cgroups and functionfs,
 *   - sets up adb, which gives us the debug channel back,
 *   - runs in permissive SELinux (androidboot.selinux=permissive), and
 *   - gives us a supervised, restartable process with no PID 1 semantics.
 *
 * So we write a MARKER FILE and exit. If the file appears in /data, our
 * userspace is running under a real init on the real device, and everything
 * downstream (MicroPython, the NFC HAL, Krux) gets to build on that instead of
 * on a hand-rolled PID 1.
 *
 * The marker deliberately goes to userdata, which is persistent, so a negative
 * is meaningful rather than an artifact of a tmpfs ramdisk.
 */
#define O_WRONLY 1
#define O_CREAT  0x40
#define O_TRUNC  0x200

#define SYS_write  4
#define SYS_open   5
#define O_RDONLY   0

typedef unsigned int u32;
#define SYS_close  6
#define SYS_read    3    /* NOT 45 -- verified in asm/unistd-eabi.h */
#define SYS_unlink 10
#define SYS_getpid 20
#define SYS_exit    1
#define SYS_stat  195   /* __NR_stat64, unused now but keep it correct */
#define SYS_mount   21
#define SYS_mkdir   39

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

/* Six-argument form, for syscalls taking a 64-bit value in an aligned
 * register pair (r3=lo, r4=hi). */
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

static void puts_hex(unsigned v)
{
    const char *h = "0123456789abcdef";
    char b[9]; b[8] = 0;
    for (int i = 7; i >= 0; i--) { b[i] = h[v & 0xf]; v >>= 4; }
    sc(SYS_write, 1, (long)b, 8, 0, 0);
}

static void puts(const char *s)
{
    int n = 0; while (s[n]) n++;
    sc(SYS_write, 1, (long)s, n, 0, 0);
}

static const char PATH[] = "/data/AIRGAP_USERSSPACE_PROOF";
static const char BODY[] =
    "AIRGAP-USERSSPACE-RAN-AS-NON-INIT-PROCESS-V1\n"
    "this file was written by a freestanding binary started by TWRP's\n"
    "Android init as an ordinary service, not as PID 1.\n";

/* /data is NOT mounted when `on boot` fires. Measured: on boot ran at
 * 08:27:56.7, but EXT4-fs (mmcblk0p16) did not mount until 08:28:01.8 --
 * five seconds later. An earlier run of this service opened the marker path
 * before /data existed, failed, and exited 0 silently.
 *
 * Rather than depend on which init trigger happens to fire late enough, wait
 * for the mount point ourselves. stat(2) on the path succeeds only once the
 * ext4 root is actually there.
 */
/* Is userdata REALLY mounted on /data yet?
 *
 * FIVE attempts, all recorded because each failure looks identical from
 * outside -- the process exits 0, silently, having written nothing.
 *
 *  1. stat("/sbin")         always succeeds: the ramdisk ships an empty /data
 *                           directory. Proves nothing.
 *  2. stat("/data/media")   fails too: /sbin/recovery contains the literal
 *                           string "/data/media" and creates it early, so it
 *                           exists on the ramdisk as well.
 *  3. statfs f_type == EF53 fails: the ramdisk's /data also reports 0xef53.
 *  4. statfs f_blocks > 0   CORRECT in isolation -- verified against every
 *                           ramdisk directory (0 blocks) and every real mount
 *                           (millions). But the loop still exited in 529 ms,
 *                           which contradicts it.
 *
 * Rather than keep guessing at a discriminator, wait on the thing that is
 * unambiguous and is guaranteed to be there once the mount is real: an inode
 * that only exists on userdata. TWRP itself creates /data/media on the real
 * filesystem once vold finishes -- observed in its own audit trail at
 * 08:46:31.9, five seconds AFTER `on boot` fires at 08:46:25.7.
 *
 * So the test is: does /data/media exist AND is it non-empty? On the ramdisk
 * TWRP may have created the directory, but nothing is in it. On real userdata
 * it holds the per-user media directories.
 *
 * If even that proves unreliable, the fallback below writes the marker to
 * BOTH /data and /tmp and reports which one worked, so the next run tells us
 * what happened instead of us inferring it.
 */
#define SYS_statfs 99      /* verified in asm/unistd-eabi.h */

static int data_ready(void)
{
    /* f_blocks at words 2-3 of the EABI struct statfs. Dumped directly:
     *   /data  w0=0000ef53 w2=00672b2a w3=000da3cb   <- 6.7M blocks
     *   /sbin  w0=858458f6 w2=00000000 w3=00000000   <- ramdisk, zero
     * Every ramdisk directory reports zero; every real mount reports millions.
     *
     * A second condition using open(O_DIRECTORY) was tried and removed: this
     * kernel returns EINVAL for O_DIRECTORY (0x10000), so the check could
     * never succeed. Measured:
     *   open("/data/media", O_RDONLY)          -> fd 3   OK
     *   open("/data/media", O_RDONLY|0x10000)  -> -22    EINVAL
     * f_blocks alone is sufficient and is what ships.
     */
    static u32 st[80];
    for (unsigned i = 0; i < 80; i++) st[i] = 0;
    if (sc6(SYS_statfs, (long)"/sbin", (long)st, 0, 0, 0, 0) != 0)
        return 0;
    return st[2] != 0 || st[3] != 0;
}

int main(void)
{
    puts("airgap: hello from a supervised service\n");

    /* /data is a tmpfs-backed empty dir until vold mounts ext4 on it, which
     * happens several seconds AFTER `on boot`. Wait for the real mount. */
    /* MEASURED, do not guess: 900 iterations of the inner spin complete in
     * 398 ms on this device (gettimeofday, on-device). The loop was sized as
     * if each iteration were ~50 ms, giving a nominal "45 s" that was in fact
     * 0.4 s -- far shorter than the ~5 s vold needs, so the wait always gave
     * up before userdata was mounted.
     *
     * 20,000 iterations ~ 8.8 s, comfortably past the measured 5.4 s gap
     * (on boot 08:46:25.7 -> p28 mounted 08:46:30.9).
     */
    for (int i = 0; i < 20000 && !data_ready(); i++)
        for (volatile long k = 0; k < 200000L; k++) ;
    puts(data_ready() ? "airgap: /data has ext4 mounted -- writing marker\n"
                      : "airgap: WARNING no ext4 on /data; marker may not persist\n");

    /* Record our PID, so we can prove we are NOT PID 1. */
    long pid = sc(SYS_getpid, 0, 0, 0, 0, 0);
    puts("airgap: my pid is not 1 (init keeps that role)\n");

    sc(SYS_unlink, (long)PATH, 0, 0, 0, 0);
    int fd = (int)sc(SYS_open, (long)PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644, 0, 0);
    if (fd < 0) {
        puts("airgap: could not create marker in /data\n");
        sc(SYS_exit, 1, 0, 0, 0, 0);
        for (;;) ;
    }
    int n = 0; while (BODY[n]) n++;
    long w = sc(SYS_write, fd, (long)BODY, n, 0, 0);
    sc(SYS_close, fd, 0, 0, 0, 0);
    puts(w == n ? "airgap: marker written OK\n" : "airgap: SHORT WRITE\n");

    /* Give the reader a moment, then exit cleanly so init can restart us. */
    for (volatile long k = 0; k < 40000000L; k++) ;
    puts("airgap: exiting via _exit (init may restart us)\n");

    /* NOT `return`: there is no crt0 and no libc here, so returning from main
     * falls off the end of the program and the kernel kills us with SIGSEGV.
     * That is exactly what init reported before ("signal 11"). */
    sc(SYS_exit, 0, 0, 0, 0, 0);
    for (;;) ;          /* unreachable; a trap beats a segfault if it happens */
}
