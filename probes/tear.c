/*
 * tear.c -- a framebuffer pattern that makes the preview tear readable.
 *
 * The camera preview shows two vertical bands at the right edge that
 * are not in the camera frame.  This program isolates the display: it
 * draws a pattern whose position and brightness encode the frame
 * number, commits it the same way the firmware does, and changes it
 * slowly enough that a human can see which part of the glass is ahead.
 *
 * It is a diagnostic, not part of the firmware.  Run it while the
 * firmware is idle (a menu or a result page, not a camera page), and
 * stop it with Ctrl-C (or let it run the frame count out).
 *
 *   tear bars     moving bars + a brightness step each frame
 *   tear flash    alternate black/white; the tear is a hard boundary
 *   tear hold     one static frame (sanity: no pattern, no tear)
 *
 * options (after the mode):
 *   --frames N        number of frames (default 300)
 *   --interval MS     delay between frames (default 120)
 *   --order rows|cols draw the framebuffer row-major or column-major
 *
 * Build (host, hard-float):
 *   arm-linux-gnueabihf-gcc -static -O2 -o /tmp/opencode/tear probes/tear.c
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>

#define FB_NODE "/dev/graphics/fb0"

/* The MSM commit, as in krux/firmware/micropython/ports/unix/lcd_bcm.c:
 * the ioctl number is derived from the structure the kernel carries
 * (two words and a struct fb_var_screeninfo, 168 bytes), magic 'm'. */
#define MSMFB_IOCTL_MAGIC 'm'

struct mdp_display_commit {
    uint32_t flags;
    uint32_t wait_for_finish;
    struct fb_var_screeninfo var;
};

#define MSMFB_DISPLAY_COMMIT _IOW(MSMFB_IOCTL_MAGIC, 164, \
                                  struct mdp_display_commit)

static int fb_fd;
static uint8_t *fb;
static size_t fb_size;
static int stride;
static int w, h;
static int pan;
static struct fb_var_screeninfo var0;

static uint32_t pix(int r, int g, int b)
{
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) |
           0xff000000u;
}

static void put(int x, int y, uint32_t p)
{
    if (x < 0 || x >= w || y < 0 || y >= h)
        return;
    *(uint32_t *)(fb + (size_t)(pan + y) * stride + (size_t)x * 4) = p;
}

static void commit(void)
{
    struct mdp_display_commit dc;
    struct fb_var_screeninfo var = var0;

    memset(&dc, 0, sizeof(dc));
    dc.flags = 0;
    dc.wait_for_finish = 1;
    dc.var = var;
    dc.var.xoffset = 0;
    dc.var.yoffset = pan;
    dc.var.xres = w;
    dc.var.yres = h;
    if (ioctl(fb_fd, MSMFB_DISPLAY_COMMIT, &dc) < 0) {
        static int once;
        if (!once) {
            once = 1;
            fprintf(stderr, "MSMFB_DISPLAY_COMMIT: %s\n", strerror(errno));
        }
    }
    /* The preview path commits with MSMFB_DISPLAY_COMMIT alone (the
     * pan is skipped there), so do the same: a second pan would add
     * another two vertical blanks and change the timing under test. */
    (void)var;
}

/* Row-major: for each row, all columns.  Column-major: for each
 * column, all rows.  The second order matches a panel that scans in
 * columns, and is the experiment. */
static int draw_order;         /* 0 rows, 1 cols */

/* The pan test: a virtual scene 25 % larger than the panel, with the
 * panel showing a 1080x1920 window that moves a few pixels per frame
 * and restarts when the window reaches the scene's edge.  The scene has
 * structure at several scales (a checkerboard for phase, diagonal
 * lines for direction, a coarser grid), so a shifted block is a visible
 * seam -- much easier for the eye than isolated bars. */
static uint32_t *tex;
static int TW, TH;             /* the virtual scene */
static int ox, oy;             /* the window's origin */
static int dx = -5, dy = -4;   /* reversed: left and up */

static void build_texture(void)
{
    int x, y;
    TW = w + w / 4;            /* 25 % larger than the panel */
    TH = h + h / 4;
    tex = malloc((size_t)TW * TH * 4);
    if (!tex) { perror("malloc texture"); exit(1); }
    for (y = 0; y < TH; y++) {
        for (x = 0; x < TW; x++) {
            int v;
            /* 12 px checkerboard (50 % larger than the 8 px one):
             * a few-pixel seam is still a visible phase break, and
             * it does not dither the eye. */
            v = (((x / 12) + (y / 12)) & 1) ? 60 : 205;
            /* diagonal line, same 50 % scale */
            if ((x + 2 * y) % 48 < 3)
                v = 20;
            tex[(size_t)y * TW + x] = pix(v, v, v);
        }
    }
}

/* Mimic the preview's draw exactly: a 1080x1080 image at (0,420),
 * flipped in both axes and converted pixel by pixel to the panel's
 * RGBX -- the same code path as lcd.display()'s fast branch.  The
 * margins are not written, as in the preview. */
#define IMG_Y 420
static void draw_img(void)
{
    int ix, iy;
    for (iy = 0; iy < 1080; iy++) {
        int sy = 1079 - iy;                 /* flip_y */
        uint8_t *drow = fb + (size_t)(pan + IMG_Y + iy) * stride;
        for (ix = 0; ix < 1080; ix++) {
            int sx = 1079 - ix;             /* flip_x */
            uint32_t p = tex[(size_t)(oy + sy) * TW + ox + sx];
            int r = p & 0xff, g = (p >> 8) & 0xff, b = (p >> 16) & 0xff;
            *(uint32_t *)(drow + (size_t)ix * 4) =
                (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) |
                0xff000000u;
        }
    }
    commit();
    ox += dx;
    if (ox < 0) ox = TW - 1080;
    if (ox > TW - 1080) ox = 0;
    oy += dy;
    if (oy < 0) oy = TH - 1080;
    if (oy > TH - 1080) oy = 0;
}

static void draw_pan(void)
{
    int y;
    uint8_t *dst = fb + (size_t)pan * stride;

    for (y = 0; y < h; y++) {
        const uint32_t *src = tex + (size_t)(y + oy) * TW + ox;
        memcpy(dst + (size_t)y * stride, src, (size_t)w * 4);
    }
    commit();
    ox += dx;
    if (ox < 0) ox = TW - w;
    if (ox > TW - w) ox = 0;
    oy += dy;
    if (oy < 0) oy = TH - h;
    if (oy > TH - h) oy = 0;
}

static void draw(int f, int mode)
{
    int x, y;
    /* Fixed brightness, dark bars: a band that is one frame ahead is
     * then just a dark bar in the wrong cell, with nothing else
     * changing under the eye. */
    const uint32_t bg = pix(170, 170, 170);
    const uint32_t grid = pix(70, 70, 70);
    const uint32_t vbar = pix(90, 90, 90);   /* dark gray */
    const uint32_t hbar = pix(20, 20, 20);   /* near black */
    /* Sweep from fully off one edge to fully off the other, so the bar
     * crosses every column instead of stopping a cell short. */
    int x0 = (int)(((long long)f * 216) % (w + 80)) - 80;
    int y0 = (int)(((long long)f * 240) % (h + 80)) - 80;

    if (draw_order == 0) {
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                uint32_t p;
                if (mode == 'f')
                    p = (f & 1) ? pix(255, 255, 255)
                                : pix(0, 0, 0);
                else if (mode == 'h')
                    p = pix(80, 80, 80);
                else {
                    p = bg;
                    if (x % 108 < 3 || y % 120 < 3)
                        p = grid;
                    if (x >= x0 && x < x0 + 80)
                        p = vbar;                   /* vertical bar */
                    if (y >= y0 && y < y0 + 80)
                        p = hbar;                   /* horizontal bar */
                }
                put(x, y, p);
            }
        }
    } else {
        for (x = 0; x < w; x++) {
            for (y = 0; y < h; y++) {
                uint32_t p;
                if (mode == 'f')
                    p = (f & 1) ? pix(255, 255, 255)
                                : pix(0, 0, 0);
                else if (mode == 'h')
                    p = pix(80, 80, 80);
                else {
                    p = bg;
                    if (x % 108 < 3 || y % 120 < 3)
                        p = grid;
                    if (x >= x0 && x < x0 + 80)
                        p = vbar;
                    if (y >= y0 && y < y0 + 80)
                        p = hbar;
                }
                put(x, y, p);
            }
        }
    }
    commit();
}

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
    int mode = 'b';
    int frames = 300;
    int interval = 120;
    int i, f;
    double t0;
    struct fb_fix_screeninfo fix;

    if (argc > 1) {
        if (!strcmp(argv[1], "flash")) mode = 'f';
        else if (!strcmp(argv[1], "hold")) mode = 'h';
        else if (!strcmp(argv[1], "bars")) mode = 'b';
        else if (!strcmp(argv[1], "pan")) mode = 'p';
        else if (!strcmp(argv[1], "img")) mode = 'i';
        else { fprintf(stderr, "mode: bars|flash|hold|pan|img\n"); return 2; }
    }
    for (i = 2; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--frames")) frames = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--interval")) interval = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--order"))
            draw_order = !strcmp(argv[i + 1], "cols");
        else if (!strcmp(argv[i], "--dx")) dx = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--dy")) dy = atoi(argv[i + 1]);
    }

    fb_fd = open(FB_NODE, O_RDWR);
    if (fb_fd < 0) { perror("open " FB_NODE); return 1; }
    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &var0) < 0) {
        perror("FBIOGET_VSCREENINFO"); return 1;
    }
    if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &fix) < 0) {
        perror("FBIOGET_FSCREENINFO"); return 1;
    }
    w = (int)var0.xres;
    h = (int)var0.yres;
    stride = (int)(fix.line_length ? fix.line_length
                                   : var0.xres * var0.bits_per_pixel / 8);
    fb_size = (size_t)fix.smem_len;
    if (!fb_size)
        fb_size = (size_t)stride * var0.yres_virtual;
    fb = mmap(NULL, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fb == MAP_FAILED) { perror("mmap"); return 1; }
    pan = (int)var0.yoffset;

    printf("fb %dx%d stride %d bpp %d pan %d size %zu\n",
           w, h, stride, var0.bits_per_pixel, pan, fb_size);
    if (mode == 'p' || mode == 'i') {
        build_texture();
        if (mode == 'i') {
            if (dx < 0) ox = TW - 1080;
            if (dy < 0) oy = TH - 1080;
        } else {
            if (dx < 0) ox = TW - w;
            if (dy < 0) oy = TH - h;
        }
        printf("scene %dx%d window %dx%d step (%d,%d)\n", TW, TH, w, h, dx, dy);
    }

    t0 = now();
    for (f = 0; f < frames; f++) {
        double a = now();
        if (mode == 'p')
            draw_pan();
        else if (mode == 'i')
            draw_img();
        else
            draw(f, mode);
        printf("frame %d ox=%d oy=%d  draw+commit %.1f ms\n",
               f, ox, oy, (now() - a) * 1e3);
        fflush(stdout);
        usleep((useconds_t)interval * 1000);
    }
    printf("done %d frames in %.1f s\n", frames, now() - t0);

    munmap(fb, fb_size);
    close(fb_fd);
    return 0;
}
