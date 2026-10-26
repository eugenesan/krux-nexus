/*
 * fbmmap.c -- write the framebuffer through mmap(), not pwrite64().
 *
 * WHY pwrite64 CANNOT WORK ON THIS DEVICE (measured, Entry 31)
 *
 * probe2 wrote a distinct value to each plane and read back six offsets:
 *
 *     wrote plane0=11111111  plane1=22222222
 *       p0 row0    off=0        val=22222222
 *       p0 row960  off=4177920  val=22222222
 *       p0 row1919 off=8351488  val=22222222
 *       p1 row1919 off=16707328 val=22222222
 *
 * Every offset returned the value written LAST. The msm_fb driver on this CAF
 * kernel ignores the write offset entirely: every pwrite64 lands in the same
 * buffer. That is why the panel stayed black with the backlight on, and why
 * every earlier "screen=PASS" was wrong -- the readback read from the same
 * place it wrote, so it could never detect this.
 *
 * THE FIX: map the device and write through the mapping. fbdev is designed to
 * be driven this way, and page offsets are honoured where the write offset was
 * not. If this also fails to change the screen, then we are not merely using
 * the wrong write interface -- the panel is not scanning fb memory at all, and
 * the mdp/dsi bring-up has to be investigated separately.
 *
 * CONSTANTS are taken from headers, not memory. Earlier tests in this project
 * used read=45, mknod=33, pwrite64=146, statfs=140 and reboot=142; all wrong.
 * Verified: mmap2=192, munmap=91, PROT_READ=1, PROT_WRITE=2, MAP_SHARED=1.
 *
 * FOLLOWING TWRP, WHICH NAMES THIS DEVICE EXPLICITLY
 * (minuitwrp/graphics_fbdev.cpp in omnirom/android_bootable_recovery):
 *
 *   "For some of those devices (eg, hammerhead aka Nexus 5),
 *    FBIOGET_VSCREENINFO *reports* that it wants a different format
 *    (XBGR) but actually produces the correct results on the display
 *    when you write RGBX."
 *
 * So the fb ioctls are NOT trustworthy here. Confirmed independently: a
 * poisoned-buffer test showed FBIOGET_FSCREENINFO returns 0 and fills nothing.
 *
 * Three corrections to my previous attempt, all from that source:
 *
 *   1. SIZE THE MAPPING FROM SYSFS, not from the ioctl. TWRP uses
 *      fi.smem_len because the ioctl fills it on normal devices. Here it does
 *      not, so we get the size from sysfs: stride x virtual_height.
 *      My previous run mapped 4096 bytes (from the ioctl's bogus 1920) and
 *      then wrote 16 MB into it -> SIGSEGV.
 *   2. WRITE THROUGH THE MAPPING with direct stores, not pwrite64 (whose
 *      offsets this driver ignores).
 *   3. UNBLANK. TWRP calls gr_fb_blank(false) during startup; we never did.
 *      /sys/class/graphics/fb0/state reads 0 -- backlight lit, panel blanked,
 *      which is exactly what a human reported seeing.
 */
typedef unsigned char u8;
typedef unsigned int  u32;

#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_RDWR   0x0002

#define SYS_exit      1
#define SYS_write     4
#define SYS_open      5
#define SYS_read      3   /* verified: 3, not 45 */
#define SYS_close     6
#define SYS_ioctl    54
#define SYS_mmap2    192        /* offset is a PAGE COUNT, not bytes */
#define SYS_munmap    91

#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define MAP_SHARED  0x01

#define FB_W      1080
#define FB_H      1920
#define FB_STRIDE 4352                 /* measured; NOT 1080*4 */
#define FB_ROWU   (FB_STRIDE / 4)
#define PAGE      4096

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

/* Six-argument form, for syscalls taking six words.
 * mmap2(addr, len, prot, flags, fd, pgoffset) needs this -- pgoffset is a
 * PAGE COUNT on ARM, not a byte offset. */
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

static void say(const char *s)
{
    int n = 0; while (s[n]) n++;
    sc(SYS_write, 1, (long)s, n, 0, 0);
}

static void saynum(long v)
{
    char b[24]; int n = 0;
    if (v < 0) { sc(SYS_write, 1, (long)"-", 1, 0, 0); v = -v; }
    if (v == 0) { sc(SYS_write, 1, (long)"0", 1, 0, 0); return; }
    while (v && n < 23) { b[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n) sc(SYS_write, 1, (long)&b[--n], 1, 0, 0);
}

static void sayhex(u32 v)
{
    const char *h = "0123456789abcdef";
    char b[9]; b[8] = 0;
    for (int i = 7; i >= 0; i--) { b[i] = h[v & 0xf]; v >>= 4; }
    sc(SYS_write, 1, (long)b, 8, 0, 0);
}

/* struct fb_fix_screeninfo, transcribed from /usr/include/linux/fb.h.
 *
 * MY FIRST VERSION OF THIS STRUCT WAS WRONG and it cost a segfault. I had
 * three __u16 fields (xpanstep, ypanstep, ywrapstep) followed by a 4-byte
 * unsigned int (line_length), which the compiler lays out WITHOUT padding,
 * giving line_length at offset 28. The kernel's layout has __u32 line_length,
 * which also lands at offset 28 -- but my `smem_len` and `line_length` read
 * back as 1920 and 16, i.e. the HEIGHT and a half-word, so every field after
 * id[] was shifted.
 *
 * The damage: smem_len came back as 1920 instead of ~16 MB, so we mapped
 * 4096 bytes (one page) and then wrote 16 MB into it. Segfault.
 *
 * The lesson, again: do not hand-write a kernel uapi struct from memory. The
 * header is on disk; copy it. Same class of error as the wrong syscall numbers
 * (read=45, mknod=33, pwrite64=146, statfs=140, reboot=142 -- all wrong) and
 * the wrong O_DIRECTORY value.
 */
struct fb_fix_screeninfo {
    char           id[16];
    unsigned long  smem_start;
    unsigned int   smem_len;
    unsigned int   type;
    unsigned int   type_aux;
    unsigned int   visual;
    unsigned short xpanstep;
    unsigned short ypanstep;
    unsigned short ywrapstep;
    unsigned int   line_length;
    unsigned long  mmio_start;
    unsigned int   mmio_len;
    unsigned int   accel;
    unsigned short capabilities;
    unsigned short reserved[2];
};

int main(void)
{
    say("fbmmap: start\n");

    int fd = (int)sc(SYS_open, (long)"/dev/graphics/fb0", O_RDWR, 0, 0, 0);
    if (fd < 0) { say("fbmmap: open fb0 failed\n"); sc(SYS_exit, 1, 0, 0, 0, 0); }
    say("fbmmap: fb0 open\n");

    /* ---- 1. size the mapping from sysfs, because the ioctl lies ------- */
    unsigned long stride = FB_STRIDE, vheight = FB_H;
    {
        int sf = (int)sc(SYS_open, (long)"/sys/class/graphics/fb0/stride", O_RDONLY, 0, 0, 0);
        if (sf >= 0) {
            char b[32]; long n = sc(SYS_read, sf, (long)b, 31, 0, 0);
            sc(SYS_close, sf, 0, 0, 0, 0);
            if (n > 0) { b[n < 31 ? n : 31] = 0; stride = 0;
                for (int i = 0; b[i] >= '0' && b[i] <= '9'; i++) stride = stride*10 + (unsigned long)(b[i]-'0'); }
        }
        int vf = (int)sc(SYS_open, (long)"/sys/class/graphics/fb0/virtual_size", O_RDONLY, 0, 0, 0);
        if (vf >= 0) {
            char b[64]; long n = sc(SYS_read, vf, (long)b, 63, 0, 0);
            sc(SYS_close, vf, 0, 0, 0, 0);
            if (n > 0) { b[n < 63 ? n : 63] = 0;
                /* format is "1080,3840" -- we want the second number */
                int seen_comma = 0; vheight = 0;
                for (int i = 0; b[i]; i++) {
                    if (b[i] == ',') { seen_comma = 1; continue; }
                    if (seen_comma && b[i] >= '0' && b[i] <= '9')
                        vheight = vheight*10 + (unsigned long)(b[i]-'0');
                }
            }
        }
        say("fbmmap: sysfs stride="); saynum((long)stride);
        say(" virtual_height="); saynum((long)vheight);
        say("\n");
    }
    if (stride == 0 || vheight == 0) { say("fbmmap: sysfs gave nothing, aborting\n"); sc(SYS_exit,1,0,0,0,0); }

    unsigned long row_bytes = stride;
    unsigned long map_len  = stride * vheight;
    map_len = (map_len + PAGE - 1) & ~(unsigned long)(PAGE - 1);
    say("fbmmap: mapping "); saynum((long)map_len); say(" bytes (");
    saynum((long)(stride * vheight)); say(" needed)\n");

    long addr = sc6(SYS_mmap2, 0, (long)map_len,
                    PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if ((unsigned long)addr >= 0xFFFFF000u) {
        say("fbmmap: mmap2 FAILED errno="); saynum(-addr); say("\n");
        sc(SYS_exit, 1, 0, 0, 0, 0);
    }
    say("fbmmap: mapped at 0x"); sayhex((u32)addr); say("\n");
    u32 *base = (u32 *)addr;

    /* ---- 2. write through the mapping ------------------------------- */
    /* TWRP writes RGBX regardless of what the ioctl claims about format,
     * and hammerhead needs exactly that. So plain 0xAARRGGBB. */
    u32 plane0 = 0xFFFF00FFu;   /* magenta */
    u32 plane1 = 0xFF00FF00u;   /* green   */
    for (unsigned long i = 0; i < map_len / 4; i++) {
        /* first half magenta, second half green: if the panel scans only one
         * buffer, the colour on screen tells us which. */
        u32 c = (i * 4 < map_len / 2) ? plane0 : plane1;
        base[i] = c;
    }
    say("fbmmap: filled mapping via direct stores (half magenta, half green)\n");

    /* ---- 3. unblank --------------------------------------------------- */
    /* TWRP: gr_fb_blank(false) during init. We never did this, and
     * /sys/class/graphics/fb0/state reads 0 -- lit backlight, blank panel. */
    /* FBIOBLANK = _IO('F', 9, unsigned int) */
    long bl = sc(SYS_ioctl, fd, 0x40044609, 0 /* FBIOBLANK_UNBLANK */, 0, 0);
    say("fbmmap: FBIOBLANK(0) rc="); saynum(bl); say("\n");
    {
        int bf = (int)sc(SYS_open, (long)"/sys/class/graphics/fb0/blank", O_WRONLY, 0, 0, 0);
        if (bf >= 0) {
            sc(SYS_write, bf, (long)"0", 1, 0, 0);
            sc(SYS_close, bf, 0, 0, 0, 0);
            say("fbmmap: wrote 0 to sysfs blank\n");
        } else {
            say("fbmmap: could not open sysfs blank ("); saynum(-bf); say(")\n");
        }
    }

    /* ---- report ------------------------------------------------------- */
    say("fbmmap: readback via mapping: first=");
    sayhex(base[100]);
    say(" mid=");
    sayhex(base[(map_len / 4) / 2]);
    say(" last=");
    sayhex(base[(map_len / 4) - 100]);
    say("\n");

    say("fbmmap: PLEASE REPORT THE SCREEN COLOUR NOW\n");
    say("fbmmap: holding the mapping; not exiting\n");

    for (;;) for (volatile long k = 0; k < 1000000L; k++) ;
}
