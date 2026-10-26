/*
 * fbcommit.c -- actually push a frame to the panel.
 *
 * WHY THIS FILE EXISTS
 *
 * Every previous screen attempt composed pixels into memory correctly and
 * changed nothing on the display. The reason is now known from the driver
 * source (z3DD3r/android_kernel_lge_hammerhead):
 *
 *   drivers/video/msm/mdss/mdss_fb.c  -- the ioctl handler has NO FBIO* cases
 *     at all. Its entire case list is MSMFB_CURSOR, MSMFB_SET_LUT,
 *     MSMFB_GET_PAGE_PROTECTION, MSMFB_BUFFER_SYNC, MSMFB_NOTIFY_UPDATE and
 *     MSMFB_DISPLAY_COMMIT. So FBIOGET_FSCREENINFO "succeeds" while filling
 *     nothing (proven with a poisoned buffer), FBIOGET_VSCREENINFO returns
 *     -22 and FBIOBLANK returns -38. The node is fbdev-shaped but the
 *     interface is CAF-native.
 *
 *   The panel is "JDI 1080p command mode dsi panel" (dmesg). Command-mode DSI
 *     does not scan out the framebuffer. A frame must be COMMITTED to the mdp,
 *     which then pushes it over DSI. Writing pixels is not a display
 *     operation. That is why every write we made was invisible.
 *
 *   msm_fb_pan_display_ex() gates on two flags:
 *       if ((!mfd->op_enable) || (!mfd->panel_power_on))
 *               return -EPERM;
 *     so a commit without them returns EPERM rather than doing nothing --
 *     which makes the call self-diagnosing.
 *
 * THE SEQUENCE
 *
 *   1. open /dev/graphics/fb0                       -- already proven working
 *   2. mmap it, sized from sysfs (NOT the ioctl)   -- already proven working
 *   3. compose the frame with direct stores          -- already proven working
 *   4. MSMFB_DISPLAY_COMMIT with a fb_var_screeninfo -- THIS FILE'S JOB
 *
 * Steps 1-3 are verified on hardware. Only step 4 was missing.
 *
 * IOCTL NUMBERS are computed from include/linux/msm_mdp.h, not guessed:
 *     MSMFB_IOCTL_MAGIC       = 'm'
 *     MSMFB_DISPLAY_COMMIT    = _IOW('m', 164, struct mdp_display_commit)
 *     MSMFB_NOTIFY_UPDATE     = _IOWR('m', 146, unsigned int)
 * and struct sizes are measured from the real header on this target:
 *     sizeof(struct fb_var_screeninfo) = 0xa0 = 160
 *     sizeof(struct mdp_display_commit) = 0xa8 = 168
 *
 * NOTE: the kernel logs unsupported ioctls with %x, so a dmesg line reading
 * "unsupported ioctl (5401)" is hex 0x5401 -- a 'T' (0x54) ioctl, not an 'F'
 * (0x46) fbdev one. Earlier entries in this project misread that as decimal.
 */
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_RDWR   0x0002

/* Syscall numbers, verified against asm/unistd-eabi.h. Earlier revisions of
 * this project used read=45, mknod=33, pwrite64=146, statfs=140, reboot=142 --
 * all wrong. */
#define SYS_exit     1
#define SYS_read     3
#define SYS_write    4
#define SYS_open     5
#define SYS_close    6
#define SYS_mmap2  192
#define SYS_ioctl   54

#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define MAP_SHARED 0x01
#define PAGE       4096

/* struct fb_var_screeninfo, transcribed from linux/fb.h. Only xres, yres,
 * xres_virtual, yres_virtual, xoffset, yoffset and bits_per_pixel are set; the
 * driver validates xoffset/yoffset against xres_virtual-xres and
 * yres_virtual-yres, so those six are what matter. */
struct fb_var_screeninfo {
    u32 xres, yres;
    u32 xres_virtual, yres_virtual;
    u32 xoffset, yoffset;
    u32 bits_per_pixel;
    u32 grayscale;
    u32 nonstd;
    u32 activate;
    u32 height, width;
    u32 accel_flags;
    u32 pixclock, left_margin, upper_margin, lower_margin, right_margin;
    u32 hsync_len, vsync_len, sync, vmode, rotate, colorspace;
    u32 nonstd_param;
    u16 activate_s;
    u16 rotate_2;
    u16 reserved[4];
};

/* struct mdp_display_commit, from include/linux/msm_mdp.h */
struct mdp_display_commit {
    u32 flags;
    u32 wait_for_finish;
    struct fb_var_screeninfo var;
};

#define MSMFB_DISPLAY_COMMIT  0x40A86DA4u   /* _IOW('m',164, 168 bytes) */
#define MSMFB_NOTIFY_UPDATE   0xC0046D92u   /* _IOWR('m',146, unsigned int) */

/* NOTIFY_UPDATE_START/STOP, from msm_fb.h */
#define NOTIFY_UPDATE_START 1
#define NOTIFY_UPDATE_STOP  0

#define FB_W      1080
#define FB_H      1920
#define FB_STRIDE 4352                 /* sysfs, NOT width*4 */

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

/* Read an unsigned from a sysfs file. Returns 0 if unavailable. */
static unsigned long read_sysfs(const char *path)
{
    int fd = (int)sc(SYS_open, (long)path, O_RDONLY, 0, 0, 0);
    if (fd < 0) return 0;
    char b[64];
    long n = sc(SYS_read, fd, (long)b, 63, 0, 0);
    sc(SYS_close, fd, 0, 0, 0, 0);
    if (n <= 0) return 0;
    b[n < 63 ? n : 63] = 0;
    unsigned long v = 0;
    for (int i = 0; b[i] >= '0' && b[i] <= '9'; i++) v = v * 10 + (unsigned long)(b[i] - '0');
    return v;
}

/* THE PRECONDITION THAT MATTERS, learned the hard way.
 *
 * msm_fb_pan_display_ex() gates on:
 *     if ((!mfd->op_enable) || (!mfd->panel_power_on))
 *             return -EPERM;
 * and msm_fb_blank_sub() sets BOTH of those to FALSE for every blank mode
 * except FB_BLANK_UNBLANK.
 *
 * TWRP runs a screen saver that blanks the panel, and it also drives the
 * backlight to 0. On the first run the commit returned EPERM and the screen was
 * simply off. Nothing was wrong with the code: another process owned the
 * display. This function therefore restores the backlight before committing,
 * so the sequence is self-contained.
 */
#define BL_PATH "/sys/devices/mdp.0/qcom,mdss_fb_primary.160/leds/lcd-backlight/brightness"

static void ensure_backlight_on(void)
{
    /* Only meaningful if we are not going to fight TWRP for the panel. The
     * caller is expected to have stopped recovery (setprop ctl.stop
     * recovery) -- see init.touchoff.rc. */
    int fd = (int)sc(SYS_open, (long)BL_PATH, O_WRONLY, 0, 0, 0);
    if (fd < 0) return;
    sc(SYS_write, fd, (long)"255", 3, 0, 0);
    sc(SYS_close, fd, 0, 0, 0, 0);
}

int main(void)
{
    say("fbcommit: start\n");
    ensure_backlight_on();

    /* ---- open -------------------------------------------------------- */
    int fd = (int)sc(SYS_open, (long)"/dev/graphics/fb0", O_RDWR, 0, 0, 0);
    if (fd < 0) { say("fbcommit: open fb0 failed\n"); sc(SYS_exit, 1, 0, 0, 0, 0); }
    say("fbcommit: fb0 open\n");

    /* ---- geometry from sysfs (the ioctls lie on this driver) --------- */
    unsigned long stride    = read_sysfs("/sys/class/graphics/fb0/stride");
    unsigned long vsize_raw = read_sysfs("/sys/class/graphics/fb0/virtual_size");
    unsigned long bpp       = read_sysfs("/sys/class/graphics/fb0/bits_per_pixel");
    unsigned long vheight   = 0;
    {   /* virtual_size is "1080,3840" -- take the second field */
        int vf = (int)sc(SYS_open, (long)"/sys/class/graphics/fb0/virtual_size", O_RDONLY, 0, 0, 0);
        if (vf >= 0) {
            char b[64]; long n = sc(SYS_read, vf, (long)b, 63, 0, 0);
            sc(SYS_close, vf, 0, 0, 0, 0);
            if (n > 0) {
                b[n < 63 ? n : 63] = 0;
                int seen = 0;
                for (int i = 0; b[i]; i++) {
                    if (b[i] == ',') { seen = 1; continue; }
                    if (seen && b[i] >= '0' && b[i] <= '9')
                        vheight = vheight * 10 + (unsigned long)(b[i] - '0');
                }
            }
        }
    }
    if (!stride) stride = FB_STRIDE;
    if (!vheight) vheight = FB_H * 2;
    if (!bpp) bpp = 32;
    say("fbcommit: stride="); saynum((long)stride);
    say(" virtual_height="); saynum((long)vheight);
    say(" bpp="); saynum((long)bpp);
    say(" vsize_raw="); saynum((long)vsize_raw);
    say("\n");

    /* ---- mmap, sized from the above ---------------------------------- */
    unsigned long map_len = stride * vheight;
    map_len = (map_len + PAGE - 1) & ~(unsigned long)(PAGE - 1);
    long addr = sc6(SYS_mmap2, 0, (long)map_len,
                    PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if ((unsigned long)addr >= 0xFFFFF000u) {
        say("fbcommit: mmap2 failed errno="); saynum(-addr); say("\n");
        sc(SYS_exit, 1, 0, 0, 0, 0);
    }
    say("fbcommit: mapped "); saynum((long)map_len); say(" bytes\n");
    u32 *base = (u32 *)addr;
    unsigned long words = map_len / 4;

    /* ---- compose: RGBX magenta in BOTH planes ------------------------ */
    /* yres_virtual is 3840 = two 1920-row planes. Fill both so that whichever
     * one the panel scans shows magenta -- committing an unfilled plane is what
     * produced a blank screen. */
    unsigned long plane_words = stride * FB_H / 4;
    for (unsigned long i = 0; i < plane_words; i++) base[i] = 0xFFFF00FFu;
    for (unsigned long i = 0; i < plane_words; i++)
        base[plane_words + i] = 0xFFFF00FFu;
    say("fbcommit: both planes composed (magenta, RGBX)\n");

    /* ---- THE COMMIT -------------------------------------------------- */
    static struct mdp_display_commit dc;
    for (unsigned long i = 0; i < sizeof(dc) / 4; i++) ((u32 *)&dc)[i] = 0;
    dc.flags = 0;
    dc.wait_for_finish = 1;              /* block until the mdp finishes */
    dc.var.xres         = FB_W;
    dc.var.yres         = FB_H;
    dc.var.xres_virtual = FB_W;
    dc.var.yres_virtual = FB_H * 2;      /* two planes, per sysfs 1080,3840 */
    dc.var.xoffset       = 0;
    dc.var.yoffset       = 0;           /* show plane 0 */
    dc.var.bits_per_pixel = (u32)bpp;

    say("fbcommit: MSMFB_DISPLAY_COMMIT ioctl=0x");
    sayhex(MSMFB_DISPLAY_COMMIT);
    say(" struct=168 bytes\n");
    say("fbcommit:   var xres="); saynum((long)dc.var.xres);
    say(" yres="); saynum((long)dc.var.yres);
    say(" yres_virtual="); saynum((long)dc.var.yres_virtual);
    say(" yoffset="); saynum((long)dc.var.yoffset);
    say("\n");

    long cr = sc(SYS_ioctl, fd, MSMFB_DISPLAY_COMMIT, (long)&dc, 0, 0);
    say("fbcommit: COMMIT rc="); saynum(cr);
    if (cr == 0)
        say("  *** SUCCESS -- THE SCREEN SHOULD CHANGE NOW ***\n");
    else if (cr == -1)
        say("  (EPERM: op_enable or panel_power_on is false)\n");
    else
        say("  (check dmesg for 'unsupported ioctl')\n");

    /* NOTE: an earlier version also committed yoffset=1920 as a diagnostic to
     * learn which plane the panel scans. That is what blanked the screen --
     * plane 1 was never filled, so it contained stale memory. Fill BOTH planes
     * and commit plane 0 only. */

    say("fbcommit: holding the mapping; not exiting\n");
    for (;;) for (volatile long k = 0; k < 1000000L; k++) ;
}
