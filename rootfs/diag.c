/* diag.c -- report the device-side USB/adb state.
 *
 * Two jobs, and only two:
 *
 *   1. Publish a SHORT status line over the USB string descriptor
 *      /sys/class/android_usb/android0/iSerial, so the host can read it with
 *      `lsusb -v` while the device is still in our kernel. A USB string
 *      descriptor holds at most 255 bytes, so this is kept deliberately
 *      small: the status plus the two kernel-log lines that decide the
 *      question.
 *
 *   2. Write the FULL report -- status, and the whole kernel log filtered to
 *      the interesting lines -- to /data/diag.txt, which persists across a
 *      reboot and can be read afterwards from the TWRP-kernel image where adb
 *      works. This is the channel that actually carries the evidence; the
 *      descriptor is only for a quick look.
 *
 * Built freestanding: raw syscalls, no libc. Static, so it runs under TWRP's
 * init as an ordinary supervised process.
 */

#define O_WRONLY   1
#define O_CREAT    0x40
#define O_TRUNC    0x200
#define O_RDONLY   0

#define SYS_write   4
#define SYS_open    5
#define SYS_close   6
#define SYS_read    3
#define SYS_mkdir   39
#define SYS_mount   21
#define SYS_access  33
#define SYS_exit    1
#define SYS_nanosleep 162
#define SYS_getdents64 217

typedef long long s64;

static long sc(long n, long a, long b, long c, long d, long e, long f)
{
    register long r7 __asm__("r7") = n;
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    register long r2 __asm__("r2") = c;
    register long r3 __asm__("r3") = d;
    register long r4 __asm__("r4") = e;
    register long r5 __asm__("r5") = f;
    __asm__ volatile("svc 0"
                     : "+r"(r0)
                     : "r"(r7), "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5)
                     : "memory");
    return r0;
}

static void put(int fd, const char *s)
{
    int n = 0;
    while (s[n]) n++;
    sc(SYS_write, fd, (long)s, n, 0, 0, 0);
}

static void putnum(int fd, long v)
{
    char b[24];
    int i = 0;
    if (v < 0) { put(fd, "-"); v = -v; }
    if (v == 0) { put(fd, "0"); return; }
    while (v && i < 20) { b[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i--) put(fd, &b[i]);
}

/* Read a small file, stripping newlines (a newline in a USB string
 * descriptor truncates it). */
static int slurp(const char *path, char *buf, int max)
{
    int fd = (int)sc(SYS_open, (long)path, O_RDONLY, 0, 0, 0, 0);
    int n = 0, o = 0;
    if (fd < 0) return -1;
    for (;;) {
        long r = sc(SYS_read, fd, (long)(buf + n), max - n - 1, 0, 0, 0);
        if (r <= 0) break;
        n += (int)r;
        if (n >= max - 1) break;
    }
    buf[n] = 0;
    sc(SYS_close, fd, 0, 0, 0, 0, 0);
    for (int i = 0; i < n; i++) {
        if (buf[i] == '\n' || buf[i] == '\r' || buf[i] == '\t') buf[i] = ' ';
        buf[o++] = buf[i];
    }
    buf[o] = 0;
    return o;
}

/* access() rather than stat(): the struct stat64 layout has to match the
 * kernel's exactly, and a hand-rolled one does not -- which is why the first
 * version waited forever for a /data/media that was there all along. */
static int exists(const char *path)
{
    return sc(SYS_access, (long)path, 0 /* F_OK */, 0, 0, 0, 0) == 0;
}

static void msleep(long ms)
{
    struct { long sec, nsec; } ts;
    ts.sec = ms / 1000;
    ts.nsec = (ms % 1000) * 1000000L;
    sc(SYS_nanosleep, (long)&ts, 0, 0, 0, 0, 0);
}

static int contains(const char *hay, const char *needle)
{
    for (int i = 0; hay[i]; i++) {
        int m = 0;
        while (needle[m] && hay[i + m] == needle[m]) m++;
        if (!needle[m]) return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ report */

static void report(int fd)
{
    char b[1024];
    char fn[128], al[128], st[128], en[64];

    fn[0] = 0; slurp("/sys/class/android_usb/android0/functions", fn, 120);
    al[0] = 0; slurp("/sys/class/android_usb/android0/f_ffs/aliases", al, 120);
    st[0] = 0; slurp("/sys/class/android_usb/android0/state", st, 60);
    en[0] = 0; slurp("/sys/class/android_usb/android0/enable", en, 20);

    put(fd, "== android_usb ==\n");
    put(fd, "  functions="); put(fd, fn); put(fd, "\n");
    put(fd, "  aliases  ="); put(fd, al); put(fd, "\n");
    put(fd, "  state    ="); put(fd, st); put(fd, "\n");
    put(fd, "  enable   ="); put(fd, en); put(fd, "\n");

    put(fd, "== endpoints ==\n");
    put(fd, "  /dev/usb-ffs/adb/ep0 : ");
    put(fd, exists("/dev/usb-ffs/adb/ep0") ? "present" : "MISSING"); put(fd, "\n");
    put(fd, "  /dev/usb-ffs/adb/ep1 : ");
    put(fd, exists("/dev/usb-ffs/adb/ep1") ? "present" : "MISSING"); put(fd, "\n");
    put(fd, "  /dev/ffs.ep0         : ");
    put(fd, exists("/dev/ffs.ep0") ? "present" : "MISSING"); put(fd, "\n");

    put(fd, "== functionfs registered? ==\n");
    {
        char fs[2048];
        fs[0] = 0;
        slurp("/proc/filesystems", fs, 2000);
        put(fd, "  /proc/filesystems mentions functionfs: ");
        put(fd, contains(fs, "functionfs") ? "yes" : "NO"); put(fd, "\n");
    }

    put(fd, "== /proc/mounts (functionfs lines) ==\n");
    {
        char mp[4096];
        mp[0] = 0;
        slurp("/proc/mounts", mp, 4000);
        int i = 0;
        int found = 0;
        while (mp[i]) {
            int st2 = i;
            while (mp[i] && mp[i] != '\n') i++;
            int en2 = i;
            if (i < 4000) i++;
            /* the line is NUL-terminated in the buffer only at the end, so
             * compare in place */
            int hit = 0;
            for (int q = st2; q < en2; q++)
                if (mp[q] == 'f' && contains(mp + q, "functionfs")) { hit = 1; break; }
            if (!hit) continue;
            for (int q = st2; q < en2; q++) sc(SYS_write, fd, (long)&mp[q], 1, 0, 0, 0);
            sc(SYS_write, fd, (long)"\n", 1, 0, 0, 0);
            found++;
        }
        if (!found) put(fd, "  (no functionfs mount)\n");
    }

    put(fd, "== kernel log (filtered) ==\n");
    {
        static char kl[65536];
        int tot = 0;
        int fd2 = (int)sc(SYS_open, (long)"/proc/kmsg", O_RDONLY, 0, 0, 0, 0);
        if (fd2 >= 0) {
            for (;;) {
                long r = sc(SYS_read, fd2, (long)(kl + tot),
                            (long)(sizeof(kl) - tot - 1), 0, 0, 0);
                if (r <= 0) break;
                tot += (int)r;
                if (tot >= (int)sizeof(kl) - 1) break;
            }
            sc(SYS_close, fd2, 0, 0, 0, 0, 0);
            const char *keys[8] = { "ffs", "android", "dwc3", "udc", "gadget",
                                     "enable", "config", "WARN" };
            int lines = 0;
            int i = 0;
            while (i < tot) {
                int st2 = i;
                while (i < tot && kl[i] != '\n') i++;
                int en2 = i;
                if (i < tot) i++;
                int hit = 0;
                for (int k = 0; k < 8 && !hit; k++)
                    for (int q = st2; q < en2; q++)
                        if (kl[q] == keys[k][0] && contains(kl + q, keys[k])) { hit = 1; break; }
                if (!hit) continue;
                for (int q = st2; q < en2; q++) sc(SYS_write, fd, (long)&kl[q], 1, 0, 0, 0);
                sc(SYS_write, fd, (long)"\n", 1, 0, 0, 0);
                lines++;
            }
            put(fd, "-- matching lines: ");
            putnum(fd, lines);
            put(fd, "\n");
        } else {
            put(fd, "  (/proc/kmsg not readable)\n");
        }
    }
}

/* ------------------------------------------------------------------ publish */

static void publish(void)
{
    char fn[128], al[128], st[128], en[64];
    char b[240];
    int n = 0;

    fn[0] = 0; slurp("/sys/class/android_usb/android0/functions", fn, 40);
    al[0] = 0; slurp("/sys/class/android_usb/android0/f_ffs/aliases", al, 40);
    st[0] = 0; slurp("/sys/class/android_usb/android0/state", st, 30);
    en[0] = 0; slurp("/sys/class/android_usb/android0/enable", en, 10);

    const char *pre = "DIAG st=";
    while (*pre && n < 230) b[n++] = *pre++;
    for (int i = 0; st[i] && n < 230; i++) b[n++] = st[i];
    const char *p2 = " en=";
    while (*p2 && n < 230) b[n++] = *p2++;
    for (int i = 0; en[i] && n < 230; i++) b[n++] = en[i];
    const char *p3 = " fn=";
    while (*p3 && n < 230) b[n++] = *p3++;
    for (int i = 0; fn[i] && n < 230; i++) b[n++] = fn[i];
    b[n++] = ' ';
    /* the two decisive kernel-log lines, and nothing else */
    {
        static char kl[65536];
        int tot = 0;
        int fd2 = (int)sc(SYS_open, (long)"/proc/kmsg", O_RDONLY, 0, 0, 0, 0);
        if (fd2 >= 0) {
            for (;;) {
                long r = sc(SYS_read, fd2, (long)(kl + tot),
                            (long)(sizeof(kl) - tot - 1), 0, 0, 0);
                if (r <= 0) break;
                tot += (int)r;
                if (tot >= (int)sizeof(kl) - 1) break;
            }
            sc(SYS_close, fd2, 0, 0, 0, 0, 0);
            const char *keys[2] = { "ffs_function", "already" };
            for (int k = 0; k < 2; k++) {
                int i = 0;
                while (i < tot) {
                    int st2 = i;
                    while (i < tot && kl[i] != '\n') i++;
                    int en2 = i;
                    if (i < tot) i++;
                    int hit = 0;
                    for (int q = st2; q < en2; q++)
                        if (kl[q] == keys[k][0] && contains(kl + q, keys[k])) { hit = 1; break; }
                    if (!hit) continue;
                    for (int q = st2; q < en2 && n < 230; q++) b[n++] = kl[q];
                    b[n++] = '|';
                    break;      /* one line per key is enough */
                }
            }
        }
    }
    b[n] = 0;

    int fd = (int)sc(SYS_open, (long)"/sys/class/android_usb/android0/iSerial",
                      O_WRONLY, 0, 0, 0, 0);
    if (fd >= 0) {
        sc(SYS_write, fd, (long)b, n, 0, 0, 0);
        sc(SYS_close, fd, 0, 0, 0, 0, 0);
    }
}

/* -------------------------------------------------------------------- main */

int main(void)
{
    /* Publish repeatedly: the android gadget may not have probed yet, and
     * /sys/class/android_usb/android0/iSerial does not exist until it has.
     * The first version of this published once, at a moment when the gadget
     * had not appeared, and published nothing at all. */
    for (int i = 0; i < 20; i++) {
        publish();
        if (exists("/sys/class/android_usb/android0/iSerial")) {
            /* the attribute is there; one more publish now that it exists */
            publish();
            break;
        }
        msleep(1000);
    }

    /* Try to create the functionfs instance ourselves. TWRP's
     * init.recovery.usb.rc does it at `on post-fs`:
     *
     *     mount functionfs adb /dev/usb-ffs/adb uid=2000,gid=2000
     *
     * but the functionfs FILESYSTEM is registered by functionfs_init(), which
     * android.c calls when the android gadget PROBES. If the gadget probes
     * after post-fs, the mount fails and there is no ep0 for adbd.
     * The kernel's mount() is (source, target, type, flags, data) and for
     * functionfs the SOURCE is the instance name.
     */
    sc(SYS_mkdir, (long)"/dev/usb-ffs", 0770, 0, 0, 0, 0);
    sc(SYS_mkdir, (long)"/dev/usb-ffs/adb", 0770, 0, 0, 0, 0);
    if (!exists("/dev/usb-ffs/adb/ep0"))
        sc(SYS_mount, (long)"adb", (long)"/dev/usb-ffs/adb",
           (long)"functionfs", 0, (long)"uid=2000,gid=2000", 0);

    /* Write the report to every persistent place that will take it, and
     * keep trying for a while: /data only appears once vold has mounted
     * userdata, which is a few seconds after boot. */
    const char *paths[5] = {
        "/data/local/tmp/diag.txt", "/data/diag.txt", "/sdcard/diag.txt",
        "/cache/diag.txt", "/tmp/diag.txt",
    };
    for (int i = 0; i < 90; i++) {
        int persistent = 0;
        for (int k = 0; k < 5; k++) {
            int fd = (int)sc(SYS_open, (long)paths[k],
                              O_WRONLY | O_CREAT | O_TRUNC, 0644, 0, 0, 0);
            if (fd < 0) continue;
            put(fd, "DIAG-BEGIN\n");
            report(fd);
            put(fd, "DIAG-END\n");
            sc(SYS_close, fd, 0, 0, 0, 0, 0);
            /* /tmp is a tmpfs and vanishes on reboot, so it does NOT count as
             * a place we have left the report. Only a persistent path does --
             * and /data only becomes available once vold has mounted
             * userdata, which is a few seconds after boot. The first version
             * of this loop broke out as soon as ANY path worked, so it wrote
             * to /tmp, exited, and left nothing behind. */
            if (k < 4) persistent = 1;
        }
        if (persistent) break;
        msleep(1000);
    }
    return 0;
}
