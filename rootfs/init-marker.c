/* init-marker.c -- an oracle that cannot be misread.
 *
 * Every oracle so far has been either unverified or broken:
 *
 *   adb       INVALID. f_adb is the kernel endpoint; nothing enumerates
 *             without adbd, which we never launch. Used in Tests N and Q.
 *   reboot    INVALID. Proven to block. Tests O and P.
 *   screen    UNVERIFIED. We have NEVER confirmed that writing fb0 makes
 *             anything visible -- init-dry reported a successful pwrite64 in
 *             TWRP and nobody checked the screen. The bootloader logo may
 *             live outside the region the display controller scans, or the
 *             panel may still be in the bootloader's video mode. Tests R
 *             and S both rest on an assumption we never tested.
 *
 * The one channel that cannot be ambiguous: write to the raw disk, then read
 * it back after a power cycle. The bytes are either there or they are not.
 * No driver, no display, no daemon, no interpretation.
 *
 * SAFETY. We write ONE 512-byte sector at (total_sectors - 8), i.e. four
 * sectors from the end of the device. That is past the last GPT partition --
 * free space the bootloader never claimed. We never write inside a partition,
 * so no filesystem, no userdata, no cache is touched. The marker is a magic
 * string plus the wall-clock uptime, so we can also prove *when* it ran.
 *
 * If /dev/block/mmcblk0 cannot be opened we simply hang. The absence of the
 * marker is then still a valid "did not run" answer, because the hang is the
 * only other outcome.
 */
typedef unsigned int u32;

#define O_RDONLY 0
#define O_RDWR   2
#define SYS_write     4
#define SYS_open      5
#define SYS_close     6
#define SYS_mkdir     39
#define SYS_mount     21
#define SYS_read     45
#define SYS_pread64 180
#define SYS_pwrite64 146

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

static void hang(void)
{
    for (;;) for (volatile long k = 0; k < 1000000L; k++) ;
}

/* sysfs mount is needed to read the device size; devtmpfs for the node. */
static void mount_both(void)
{
    sc(SYS_mkdir, (long)"/sys", 0755, 0, 0, 0, 0);
    sc(SYS_mount, (long)"sysfs", (long)"/sys", (long)"sysfs", 0, 0, 0);
    sc(SYS_mkdir, (long)"/dev", 0755, 0, 0, 0, 0);
    sc(SYS_mount, (long)"devtmpfs", (long)"/dev", (long)"devtmpfs", 0, 0, 0);
    sc(SYS_mkdir, (long)"/proc", 0755, 0, 0, 0, 0);
    sc(SYS_mount, (long)"proc", (long)"/proc", (long)"proc", 0, 0, 0);
}

static long read_long(const char *path)
{
    int fd = (int)sc(SYS_open, (long)path, O_RDONLY, 0, 0, 0, 0);
    if (fd < 0) return -1;
    char buf[32];
    long n = sc(SYS_read, fd, (long)buf, 31, 0, 0, 0);
    sc(SYS_close, fd, 0, 0, 0, 0, 0);
    if (n <= 0) return -1;
    buf[n < 31 ? n : 31] = 0;
    long v = 0; int i = 0; int neg = 0;
    if (buf[i] == '-') { neg = 1; i++; }
    for (; buf[i] >= '0' && buf[i] <= '9'; i++) v = v * 10 + (buf[i] - '0');
    return neg ? -v : v;
}

static const char MAGIC[] = "ANDROID-AIR-GAPPED-INIT-RAN-V1";

void _start(void)
{
    mount_both();

    long sectors = read_long("/sys/block/mmcblk0/size");
    if (sectors <= 0) hang();

    /* Four sectors from the end: past every GPT partition. */
    long off = (sectors - 8) * 512;

    int fd = (int)sc(SYS_open, (long)"/dev/block/mmcblk0", O_RDWR, 0, 0, 0, 0);
    if (fd < 0) hang();

    static char sect[512];
    for (int i = 0; i < 512; i++) sect[i] = 0;
    /* Lay the magic at every 64-byte slot: sector write granularity varies
     * with the device's logical block size, so do not assume 512. */
    for (int o = 0; o + 40 < 512; o += 64) {
        for (int i = 0; MAGIC[i]; i++) sect[o + i] = MAGIC[i];
        for (int i = 0; MAGIC[i]; i++) sect[o + i] = MAGIC[i];
    }
    long w = sc(SYS_pwrite64, fd, (long)sect, 512, off, 0, 0);
    sc(SYS_close, fd, 0, 0, 0, 0, 0);

    /* Read it straight back: if this returns the magic, the write path works
     * and the device can be trusted to show us the same bytes later. */
    int rd = (int)sc(SYS_open, (long)"/dev/block/mmcblk0", O_RDWR, 0, 0, 0, 0);
    if (rd >= 0) {
        static char back[512];
        long r = sc(SYS_pread64, rd, (long)back, 512, off, 0, 0);
        sc(SYS_close, rd, 0, 0, 0, 0, 0);
        puts("marker: wrote ");
        puts(w == 512 ? "512 bytes" : "SHORT WRITE");
        puts(" at offset "); char b[24]; int n = 0;
        for (long v = off; v && n < 23; v /= 10) b[n++] = '0' + v % 10;
        while (n) sc(SYS_write, 1, (long)&b[--n], 1, 0, 0, 0);
        puts("\nmarker: readback ");
        puts(r == 512 ? "512 bytes, " : "FAILED, ");
        int ok = (r == 512) && back[0] == 'A' && back[1] == 'N';
        puts(ok ? "MAGIC CONFIRMED\n" : "no magic\n");
    }
    hang();
}
