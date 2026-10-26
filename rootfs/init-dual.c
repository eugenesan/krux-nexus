/* init-dual.c -- a marker that cannot be confused with a failed mount.
 *
 * WHY TEST V WAS STILL AMBIGUOUS
 *
 * Our init wrote /mnt/ud/INIT_RAN_V1 whether or not the ext4 mount had
 * succeeded. If the mount failed, the file was created on the ramdisk and
 * vanished at reboot. So "marker absent" meant EITHER "init never ran" OR
 * "init ran and the mount failed" -- the exact ambiguity this test was built
 * to remove. I put it back in by making the write unconditional.
 *
 * THE FIX: two markers, and the first one needs nothing but mknod + write.
 *
 *   Stage 1 -- raw sector write to partition p7 ("pad", LBA 138240, 2 MB, a
 *              partition nobody mounts; confirmed no kernel message ever
 *              references it). No filesystem, no mount, no ext4, no FBE, no
 *              SELinux file-context lookup. Its offset is
 *              138240 * 512 = 70,778,880, which fits in 32 bits, so this
 *              cannot possibly repeat Test T's 64-bit truncation bug.
 *
 *   Stage 2 -- the userdata file, exactly as before.
 *
 * READING THE RESULT
 *
 *   p7 marker present, userdata file absent
 *         -> init RAN. The ext4 mount of userdata is what failed. Now we have
 *            a real diagnosis to chase (FBE? SELinux? mount flags?).
 *   both present
 *         -> init ran and everything works.
 *   neither present
 *         -> init did NOT run as PID 1. The problem is upstream of every
 *            syscall we have tried.
 *
 * That third case is the one we have never actually been able to establish.
 */
typedef unsigned int u32;

#define S_IFBLK  0x6000
#define O_WRONLY 1
#define O_RDWR   2
#define O_CREAT  0x40
#define O_TRUNC  0x200

#define SYS_mknod  33
#define SYS_write  4
#define SYS_open   5
#define SYS_close  6
#define SYS_mkdir 39
#define SYS_mount 21

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

/* 64-bit-capable form: the offset is an aligned register PAIR (r3=lo, r4=hi).
 * Test T truncated its offset to 32 bits because it used the 5-arg form. */
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

static long makedev(long maj, long min)
{
    return ((((maj & 0xfffL) << 8) | (min & 0xffL))) & 0xffffffffL;
}

static void puts(const char *s)
{
    int n = 0; while (s[n]) n++;
    sc(SYS_write, 1, (long)s, n, 0, 0);
}

static void hang(void)
{
    for (;;) for (volatile long k = 0; k < 1000000L; k++) ;
}

static const char M1[] = "STAGE1-PAD-PARTITION-WRITE-PROOF-V1";
static const char M2[] = "STAGE2-USERDATA-FILE-PROOF-V1";

/* Fill the sector with the magic at 64-byte slots so the write lands
 * recognisably regardless of the device's logical block size. */
static void fill(char *buf, const char *m)
{
    for (int i = 0; i < 512; i++) buf[i] = (char)0xa5;
    for (int o = 0; o + 40 < 512; o += 64)
        for (int i = 0; m[i]; i++) buf[o + i] = m[i];
}

void _start(void)
{
    /* ---- STAGE 1: raw write to p7 "pad". Nothing can make this fail
     * silently except init not running at all. -------------------------- */
    sc(SYS_mkdir, (long)"/dev", 0755, 0, 0, 0);
    sc6(SYS_mknod, (long)"/dev/mmcblk0p7", S_IFBLK | 0644, makedev(179, 7), 0, 0, 0);
    puts("stage1: mknod p7 done\n");

    static char s1[512];
    fill(s1, M1);
    int fd = (int)sc(SYS_open, (long)"/dev/mmcblk0p7", O_RDWR, 0, 0, 0);
    puts(fd < 0 ? "stage1: OPEN FAILED\n" : "stage1: opened p7\n");
    if (fd >= 0) {
        /* LBA 138240 is where p7 begins; offset 0 within the partition.
         * 138240*512 = 70,778,880 fits in 32 bits, but pass it as a proper
         * 64-bit pair anyway. */
        long lo = 0, hi = 0;      /* offset 0 within the partition node */
        long w = sc6(142, fd, (long)s1, 512, lo, hi, 0);   /* __NR_pwrite64 */
        puts(w == 512 ? "stage1: WROTE 512 to p7\n" : "stage1: pwrite failed\n");
        sc(SYS_close, fd, 0, 0, 0, 0);
    }

    /* ---- STAGE 2: mount userdata and write a file ---------------------- */
    sc6(SYS_mknod, (long)"/dev/mmcblk0p28", S_IFBLK | 0644, makedev(179, 28), 0, 0, 0);
    sc(SYS_mkdir, (long)"/mnt", 0755, 0, 0, 0);
    sc(SYS_mkdir, (long)"/mnt/ud", 0755, 0, 0, 0);
    long m = sc(SYS_mount, (long)"/dev/mmcblk0p28", (long)"/mnt/ud",
                (long)"ext4", 0, 0);
    puts(m == 0 ? "stage2: mount OK\n" : "stage2: MOUNT FAILED\n");
    int f2 = (int)sc(SYS_open, (long)"/mnt/ud/STAGE2_PROOF",
                     O_WRONLY | O_CREAT | O_TRUNC, 0644, 0, 0);
    if (f2 >= 0) {
        int n = 0; while (M2[n]) n++;
        sc(SYS_write, f2, (long)M2, n, 0, 0);
        sc(SYS_close, f2, 0, 0, 0, 0);
        puts("stage2: file written\n");
    } else {
        puts("stage2: could not create file (expected if mount failed)\n");
    }
    hang();
}
