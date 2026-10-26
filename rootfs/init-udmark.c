/* init-udmark.c -- a safe, unambiguous oracle.
 *
 * WHY THE RAW-SECTOR PLAN IS DEAD (Test T's postmortem):
 *
 *   The GPT was read directly (backup header at LBA 61071359). The disk is
 *   FULLY ALLOCATED: userdata ends at LBA 61071315, grow at 61071326, backup
 *   GPT at 61071359. There is no free space at the end -- my earlier claim of
 *   "22 GB unallocated" came from reading /proc/partitions, which reports
 *   SIZES, not offsets, and I assumed a 1 MB start. Both wrong. That is also
 *   why the kernel answered `Invalid argument`: there was nothing to write to.
 *
 *   Raw writes additionally need true 64-bit offsets (the pwrite64 offset is
 *   an ARM register *pair*), which is the bug that put Test T's marker inside
 *   userdata. Do not go back to raw sectors.
 *
 * THE FIX: let the kernel mount the filesystem instead. ext4 is built into
 * this kernel, so mount(2) does the hard work and every offset stays 32-bit.
 * We write ONE small file into /data. That is benign -- it is an ordinary file
 * in the user's data partition, removable with rm, and it cannot corrupt
 * anything the way a raw sector write inside a mounted filesystem can.
 *
 * WHAT IT PROVES:
 *   file present   -> our binary ran as PID 1. No driver ambiguity, no
 *                     display, no adbd, no interpretation. The kernel found
 *                     the file we asked it to write.
 *   file absent    -> it did not run as PID 1.
 *
 * CONTROL: the same binary built with -DCONTROL_BUILD writes a DIFFERENT file
 * name. Running that inside TWRP proves the whole path (mount, open, write)
 * works on this device before we trust any negative result -- which is
 * precisely what Test T failed to do.
 */
typedef unsigned int u32;

#define S_IFBLK  0x6000
#define S_IFCHR  0x2000

#define O_WRONLY 1
#define O_CREAT  0x40
#define O_TRUNC  0x200

#define SYS_write   4
#define SYS_open    5
#define SYS_close   6
#define SYS_mknod  33
#define SYS_mkdir  39
#define SYS_mount  21

/* All syscalls used here take at most 5 arguments, so r0-r4 suffice. Keeping
 * the wrapper at exactly 5 avoids the arity mistakes of earlier revisions. */
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

/* Six-argument form, for syscalls taking a 64-bit value in an aligned register
 * pair. mknod's dev_t and pwrite64's offset both need this -- getting it wrong
 * is what put Test T's marker inside userdata (Entry 22). */
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

/* makedev encoding, matching glibc: dev_t is
 * ((major & 0xfffff00) << 32) | ((major & 0xfff) << 8) | (minor & 0xffffff00)
 * | (minor & 0xff). Our majors/minors are small, so the low 32 bits are all
 * that matter, but assemble it correctly anyway. */
static long makedev(long maj, long min)
{
    return (((maj & 0xfffff00ULL) << 32) |
            ((maj & 0xfffULL) << 8) |
            ((min & 0xffffff00ULL) << 12) |
            ((min & 0xffULL))) & 0xffffffffL;
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

#ifdef CONTROL_BUILD
static const char DIR[]  = "/mnt/ctl";
static const char FILEP[] = "/mnt/ctl/CONTROL_PROOF";
static const char BODY[] = "ANDROID-AIR-GAPPED-CONTROL-WORKED-V1";
#else
static const char DIR[]  = "/mnt/ud";
static const char FILEP[] = "/mnt/ud/INIT_RAN_V1";
static const char BODY[] = "ANDROID-AIR-GAPPED-INIT-RAN-AS-PID1-V1";
#endif

/* devtmpfs names the partition /dev/mmcblk0p28; Android's ueventd normally
 * also creates /dev/block/mmcblk0p28, but ueventd has not run yet under our
 * init, so try the devtmpfs name first and the block/ name as a fallback. */
static const char *CANDIDATES[] = {
    "/dev/mmcblk0p28", "/dev/block/mmcblk0p28", 0
};

void _start(void)
{
    /* CRITICAL, LEARNED THE HARD WAY: this kernel has NO devtmpfs.
     * /proc/filesystems lists only `nodev tmpfs` and `ext4`. Every init since
     * Test S began by mounting devtmpfs on /dev; that mount ALWAYS failed,
     * leaving /dev an empty ramdisk directory. So /dev/graphics/fb0,
     * /dev/bcm2079x and /dev/block/* did not exist, every init took its
     * error path, and it hung silently with no way to report anything.
     *
     * That is the likely reason for a whole series of identical hangs.
     *
     * TWRP's /dev is a tmpfs populated with mknod'd nodes -- that is why the
     * control worked there and our init could never work. We must create the
     * device nodes ourselves with mknod(2).
     */
    /* DO NOT mount anything on /dev.
     *
     * Verified in TWRP: mounting tmpfs on /dev SUCCEEDS, which silently
     * replaces the live system's entire /dev -- including /dev/block and every
     * mknod'd node. Our first attempt did exactly that, which is why both
     * mount candidates then failed and the marker landed on a tmpfs that
     * vanished on reboot. dmesg showed it plainly:
     *     [99.999405] SELinux: initialized (dev tmpfs, type tmpfs)
     *
     * Our initramfs ships an empty /dev directory, so there is nothing to
     * replace and nothing to preserve. Just mknod what we need, in place.
     * Under a real boot this is both correct and harmless.
     */
    sc(SYS_mkdir, (long)"/dev", 0755, 0, 0, 0);

    /* Create the node we need by hand: major 179 minor 28 = mmcblk0p28
     * (userdata), confirmed from /proc/partitions on the device. */
    sc6(SYS_mknod, (long)"/dev/mmcblk0p28", S_IFBLK | 0644,
        makedev(179, 28), 0, 0, 0);

    sc(SYS_mkdir, (long)"/mnt", 0755, 0, 0, 0);
    sc(SYS_mkdir, (long)DIR, 0755, 0, 0, 0);

    int fd = -1;
    for (int i = 0; CANDIDATES[i]; i++) {
        long m = sc(SYS_mount, (long)CANDIDATES[i], (long)DIR,
                    (long)"ext4", 0, 0);
        puts("mount ");
        puts(CANDIDATES[i]);
        puts(" -> ");
        puts(m == 0 ? "OK" : "failed");
        puts("\n");
        if (m == 0) break;
    }
    /* If every mount failed we still try the open: on some kernels userdata
     * is already mounted by the time init runs. */
    fd = (int)sc(SYS_open, (long)FILEP, O_WRONLY | O_CREAT | O_TRUNC, 0644, 0, 0);
    if (fd < 0) { puts("could not create marker file\n"); hang(); }

    int n = 0; while (BODY[n]) n++;
    long w = sc(SYS_write, fd, (long)BODY, n, 0, 0);
    sc(SYS_close, fd, 0, 0, 0, 0);
    puts("wrote ");
    puts(w == n ? "marker OK" : "SHORT WRITE");
    puts(" to ");
    puts(FILEP);
    puts("\n");
    hang();
}
