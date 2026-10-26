/*
 * init.c -- minimal static init for Phase 2, bare Linux on the stock hammerhead
 * kernel.
 *
 * Written in C, statically linked, on purpose. The previous attempt used a
 * shell script, which put a shebang, a shell interpreter, a dynamically linked
 * busybox and glibc between the kernel and anything visible. nfc-probe already
 * proved that a static ARM binary runs on this device, so a static init removes
 * every one of those variables at once.
 *
 * Order matters. Framebuffer FIRST, so that if anything after this fails we
 * still know we got here. Then USB, then the NFC bring-up, then a heartbeat.
 * Each stage paints a distinct bar so the screen alone tells us how far we got.
 *
 * Build:
 *   arm-linux-gnueabihf-gcc -O2 -static -o init init.c
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* MEASURED on this device, do not assume: */
#define FB_W     1080
#define FB_H     1920
#define FB_BPP   32
/* The one dangerous number. 1080*4 = 4320; the real stride is 4352, i.e. 32
 * bytes of padding per line. Using width*4 shears the image by 32 bytes per
 * row and the result still looks like a plausible rendering bug. */
#define FB_STRIDE 4352

#define FB0 "/dev/graphics/fb0"
#define A   "/sys/class/android_usb/android0"

static int fb = -1;

/* 0x00RRGGBB as ARGB8888 little-endian. mdsssfb is 32bpp; we fill solid
 * colours so exact channel order does not matter, only that it is not 0. */
#define RGB(r, g, b) ((uint32_t)0xFF000000u | ((uint32_t)(b) << 16) | \
                      ((uint32_t)(g) << 8) | (uint32_t)(r))

static void fb_bar(int y0, int y1, uint32_t colour)
{
    if (fb < 0) return;
    /* One row of the colour at the true stride... */
    uint32_t row[FB_STRIDE / 4];
    for (int i = 0; i < FB_STRIDE / 4; i++) row[i] = colour;
    for (int y = y0; y < y1 && y < FB_H; y++)
        if (pwrite(fb, row, FB_STRIDE, (off_t)y * FB_STRIDE) < 0) return;
}

/* Draw a progress ladder: bar N of 8 gets a distinct colour as stages pass. */
static int stage = 0;
static void fb_stage(void)
{
    int y0 = stage * (FB_H / 8);
    int y1 = y0 + (FB_H / 8);
    fb_bar(y0, y1, RGB(0x20 + stage * 28, 0x60, 0xC0 - stage * 24));
    stage++;
}

static void say(const char *s)
{
    /* Console AND framebuffer, so ttyHSL0 works if a cable ever appears. */
    write(STDOUT_FILENO, s, strlen(s));
}

static void step(const char *what)
{
    say("STEP: ");
    say(what);
    say("\n");
    fb_stage();
}

static void mount_it(const char *src, const char *tgt, const char *type)
{
    mkdir(tgt, 0755);
    if (mount(src, tgt, type, 0, NULL) != 0)
        say("  mount FAILED: ");
    else
        say("  mounted ");
    say(tgt);
    say("\n");
}

int main(void)
{
    say("\n=== static C init on hammerhead ===\n");

    fb = open(FB0, O_RDWR);
    if (fb < 0) {
        say("!! cannot open " FB0 ": ");
        say(strerror(errno));
        say("\n   continuing without framebuffer\n");
    } else {
        /* Paint the whole screen black first: proves write() reaches the
         * driver at all, and that we picked the right offset. */
        fb_bar(0, FB_H, RGB(0, 0, 0));
        say("framebuffer: painted black, 1080x1920 stride=4352 (width*4 would be 4320)\n");
        fb_stage();
    }

    step("mounting pseudo filesystems");
    mount_it("proc",     "/proc", "proc");
    mount_it("sysfs",    "/sys",  "sysfs");
    mount_it("devtmpfs", "/dev",  "devtmpfs");
    mkdir("/tmp", 0755);
    mount_it("tmpfs",    "/tmp",  "tmpfs");

    step("checking device nodes");
    struct { const char *p; } nodes[] = {
        {"/dev/bcm2079x"}, {"/dev/graphics/fb0"}, {"/dev/input/event1"}, {NULL}
    };
    for (int i = 0; nodes[i].p; i++) {
        struct stat st;
        say("  ");
        say(stat(nodes[i].p, &st) == 0 ? "OK      " : "MISSING ");
        say(nodes[i].p);
        say("\n");
    }

    step("USB gadget: writing 'adb acm'");
    int a = open(A, O_RDONLY);
    if (a < 0) {
        say("  " A " missing\n");
    } else {
        close(a);
        FILE *f;
        struct { const char *path; const char *val; } w[] = {
            {"/sys/class/android_usb/android0/enable",     "0"},
            {"/sys/class/android_usb/android0/idVendor",   "0x18d1"},
            {"/sys/class/android_usb/android0/idProduct",  "0x4ee2"},
            {"/sys/class/android_usb/android0/functions",  "adb acm"},
            {"/sys/class/android_usb/android0/enable",     "1"},
            {NULL, NULL}
        };
        for (int i = 0; w[i].path; i++) {
            f = fopen(w[i].path, "w");
            if (!f) { say("  write failed: "); say(w[i].path); say("\n"); continue; }
            fputs(w[i].val, f);
            fclose(f);
            say("  wrote ");
            say(w[i].path);
            say(" <- ");
            say(w[i].val);
            say("\n");
        }
        /* read back what the driver actually took */
        f = fopen("/sys/class/android_usb/android0/functions", "r");
        if (f) {
            char buf[64] = {0};
            if (fgets(buf, sizeof buf, f)) { say("  READBACK functions=["); say(buf);
                                             fclose(f); while (buf[strlen(buf) ? strlen(buf)-1 : 0] == '\n') {} say("]\n"); }
        }
    }

    step("NFC: /dev/bcm2079x");
    int n = open("/dev/bcm2079x", O_RDWR | O_NOCTTY);
    if (n < 0) {
        say("  open failed: ");
        say(strerror(errno));
        say("\n");
    } else {
        say("  opened OK fd=");
        char d[16];
        snprintf(d, sizeof d, "%d", n);
        say(d);
        say("\n");
        /* The ioctls, exactly as in nfc-probe.c */
        unsigned long reqs[] = { 0xFA01, 0xFA05, 0xFA07, 0xFA04 };
        const char *nm[]  = { "POWER_CTL", "WAKE_CTL", "SET_CLIENT_ADDR",
                              "SET_WAKE_ACTIVE_STATE" };
        unsigned long vals[] = { 1, 1, 1, 1 };
        for (int i = 0; i < 4; i++) {
            int r = ioctl(n, reqs[i], vals[i]);
            say("  ioctl ");
            say(nm[i]);
            say(r == 0 ? " -> OK\n" : " -> FAILED\n");
        }
        close(n);
    }

    step("heartbeat: 3s per bar, cycling 8 bars");
    for (int i = 0; i < 60; i++) {
        stage = i % 8;
        fb_bar(0, FB_H, RGB(0, 0, 0));
        fb_stage();
        struct timespec ts = { 3, 0 };
        nanosleep(&ts, NULL);
    }

    say("=== init loop finished ===\n");
    return 0;
}
