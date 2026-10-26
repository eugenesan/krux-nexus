/*
 * nfc-probe.c -- talk to /dev/bcm2079x directly, with no Android involved.
 *
 * Purpose: walk the BCM2079x bring-up sequence by hand and confirm each step
 * against the real chip. Everything here is derived from the Broadcom HAL
 * source (halimpl/bcm2079x on AOSP) and cross-checked against the HAL binary
 * that was pulled off this device. See b/nfc/TRANSPORT.md.
 *
 * Build (static, so it runs under bionic AND under a bare initramfs):
 *   arm-linux-gnueabihf-gcc -O2 -static -o nfc-probe nfc-probe.c
 *
 * Modes:
 *   --info             open the device, change nothing
 *   --power            power/wake ioctl reachability only
 *   --listen [secs]    power on and listen, send nothing
 *   --reset            just the power/wake reset sequence, no I/O
 *   --bringup          the full sequence (see BRINGUP below)
 *   --send HEX         write these bytes (e.g. "10 2f 04 00"), print the reply
 *   --poke [secs]      reset, then get_build_info, print every packet
 *
 * BRINGUP, from userial_linux.c:1700 and nfc_hal_dm.c:
 *   WAKE_CTL  NFC_WAKE_ASSERTED_ON_POR (UPIO_OFF = 0)
 *   POWER_CTL 0
 *   delay 10 ms
 *   POWER_CTL 1
 *   -> chip sends  10 60 00 02 00 01      Core Reset notification
 *   write         10 2F 04 00            GET_BUILD_INFO (proprietary GID 0x0F)
 *   -> chip replies MT_RSP
 *
 * Note READ_MULTI_PACKETS is deliberately NOT issued: bcmi2cnfc_read_multi_
 * packets defaults to 0 and the ROM's libnfc-nci-20791b05.conf does not set
 * it, so the real HAL does not call that ioctl either.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/ioctl.h>

/* Kernel ARM ioctl encoding: _IO(type,nr) == (type << 8) | nr. Hardcoded so
 * the numbers are visible and cannot drift. */
#define BCMNFC_POWER_CTL              0xFA01
#define BCMNFC_CHANGE_ADDR            0xFA02
#define BCMNFC_READ_FULL_PACKET       0xFA03
#define BCMNFC_SET_WAKE_ACTIVE_STATE  0xFA04
#define BCMNFC_WAKE_CTL               0xFA05
#define BCMNFC_READ_MULTI_PACKETS     0xFA06
#define BCMNFC_SET_CLIENT_ADDR        0xFA07

#define NFC_DEV     "/dev/bcm2079x"
#define MIN_BUFSIZE 259

/* From nci_defs.h: MT 0xE0>>5, PBF 0x10>>4, CID 0x0F; OID mask 0x3F. */
#define MT_CMD 0x20
#define MT_RSP 0x40
#define MT_NTF 0x60
#define GID_CORE 0x00
#define GID_PROP 0x0F

/* Extracted from the shipped HAL binary's .rodata at 0x9a88:
 *   nfc_hal_dm_get_build_info_cmd      = 2F 04 00
 *   nfc_hal_dm_get_patch_version_cmd  = 2F 2D 00
 * GET_BUILD_INFO = 0x04. */
static const unsigned char CMD_GET_BUILD_INFO[]     = { 0x10, 0x2F, 0x04, 0x00 };
__attribute__((unused))
static const unsigned char CMD_GET_PATCH_VERSION[] = { 0x10, 0x2F, 0x2D, 0x00 };

static int g_fd = -1;

static void msleep(int ms)
{
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static void hexdump(const unsigned char *b, int n)
{
    for (int i = 0; i < n; i += 16) {
        printf("      %04x  ", i);
        for (int j = 0; j < 16; j++) {
            if (i + j < n) printf("%02x ", b[i + j]);
            else          printf("   ");
        }
        printf(" |");
        for (int j = 0; j < 16 && i + j < n; j++)
            printf("%c", (b[i+j] >= 0x20 && b[i+j] < 0x7f) ? b[i+j] : '.');
        printf("|\n");
    }
}

static void describe(const unsigned char *b, int n)
{
    if (n < 1) { printf("      framing: no bytes\n"); return; }

    if (b[0] == 0x10)      printf("      HCIT type: NFC (0x10)\n");
    else if (b[0] == 0x04) printf("      HCIT type: EVENT (0x04)\n");
    else { printf("      HCIT type: UNKNOWN 0x%02x\n", b[0]); return; }

    int hdr = (b[0] == 0x10) ? 4 : 3;
    if (n < hdr) { printf("      short: %d bytes\n", n); return; }

    const unsigned char *nci = b + 1;
    const char *mt = (nci[0] & 0xE0) == MT_NTF ? "MT_NTF" :
                     (nci[0] & 0xE0) == MT_RSP ? "MT_RSP" :
                     (nci[0] & 0xE0) == MT_CMD ? "MT_CMD" : "MT_???";
    const char *gid = (nci[0] & 0x0F) == GID_PROP ? "PROP(0x0F)" :
                      (nci[0] & 0x0F) == GID_CORE ? "CORE(0x00)" : "other";
    unsigned oid = nci[1] & 0x3F;
    unsigned len = nci[2];

    printf("      NCI: %s  PBF=%d  CID=0x%x  GID=%s  OID=0x%02x  len=%u  (got %u) %s\n",
           mt, (nci[0] & 0x10) >> 4, nci[0] & 0x0F, gid, oid, len,
           (unsigned)(n - hdr), (len == (unsigned)(n - hdr)) ? "MATCH" : "MISMATCH");
    if (oid == 0x00 && (nci[0] & 0xE0) == MT_NTF)
        printf("      --> CORE_RESET notification (power-up)\n");
    if (oid == 0x04 && (nci[0] & 0xE0) == MT_RSP)
        printf("      --> response to GET_BUILD_INFO\n");
}

static int try_ioctl(int fd, unsigned long req, unsigned long arg)
{
    int r = ioctl(fd, req, arg);
    printf("    ioctl 0x%04lx arg=0x%lx -> %s\n", req, arg,
           r == 0 ? "OK" : strerror(errno));
    return r;
}

/* The reset sequence, exactly as userial_linux.c does it. Order matters:
 * wake is set BEFORE power is raised. */
static void do_reset(void)
{
    ioctl(g_fd, BCMNFC_WAKE_CTL, 0);   /* NFC_WAKE_ASSERTED_ON_POR = UPIO_OFF */
    ioctl(g_fd, BCMNFC_POWER_CTL, 0);
    msleep(10);                        /* GKI_delay(10) */
    ioctl(g_fd, BCMNFC_POWER_CTL, 1);
}

/* Read one packet, waiting up to timeout_ms. Returns bytes read, 0 on
 * timeout, -1 on error. */
static int read_packet(unsigned char *buf, int max, int timeout_ms)
{
    struct pollfd p = { .fd = g_fd, .events = POLLIN };
    int pr = poll(&p, 1, timeout_ms);
    if (pr < 0) return -1;
    if (pr == 0) return 0;
    int n = read(g_fd, buf, max);
    return n < 0 ? -1 : n;
}

static void show(const char *tag, const unsigned char *b, int n)
{
    printf("  %s: %d bytes\n", tag, n);
    hexdump(b, n);
    describe(b, n);
}

static int wait_for(int want_oid, int want_mt, int timeout_ms,
                    const char *what)
{
    unsigned char buf[MIN_BUFSIZE];
    for (int waited = 0; waited < timeout_ms; waited += 100) {
        int n = read_packet(buf, sizeof buf, 100);
        if (n < 0) { printf("  read error: %s\n", strerror(errno)); return -1; }
        if (n == 0) continue;
        show(what, buf, n);
        if (n >= 4) {
            unsigned mt  = buf[1] & 0xE0;
            unsigned oid = buf[2] & 0x3F;
            if (mt == want_mt && oid == want_oid) return 0;
        }
        printf("    (not %s; waiting)\n", what);
    }
    printf("  !! timed out after %d ms waiting for %s\n", timeout_ms, what);
    return -1;
}

static int write_all(const unsigned char *b, int n)
{
    int total = 0;
    while (total < n) {
        int r = write(g_fd, b + total, n - total);
        if (r <= 0) { printf("  write failed: %s\n", strerror(errno)); return -1; }
        total += r;
    }
    printf("  wrote %d bytes: ", total);
    for (int i = 0; i < total; i++) printf("%02x ", b[i]);
    printf("\n");
    return 0;
}

static void power_off_on_exit(void)
{
    if (g_fd >= 0) {
        ioctl(g_fd, BCMNFC_POWER_CTL, 0);
        close(g_fd);
        g_fd = -1;
        printf("chip powered off\n");
    }
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "--info";
    unsigned char buf[MIN_BUFSIZE];
    int secs = argc > 2 ? atoi(argv[2]) : 30;

    atexit(power_off_on_exit);

    printf("== open %s ==\n", NFC_DEV);
    g_fd = open(NFC_DEV, O_RDWR | O_NOCTTY);
    if (g_fd < 0) {
        printf("   FAILED: %s (need root)\n", strerror(errno));
        return 1;
    }
    printf("   OK fd=%d\n\n", g_fd);

    if (!strcmp(mode, "--info")) {
        printf("device is openable; changing nothing.\n");
        return 0;
    }

    if (!strcmp(mode, "--power")) {
        printf("== ioctl reachability ==\n");
        try_ioctl(g_fd, BCMNFC_POWER_CTL, 1);
        try_ioctl(g_fd, BCMNFC_WAKE_CTL, 1);
        try_ioctl(g_fd, BCMNFC_SET_CLIENT_ADDR, 1);
        try_ioctl(g_fd, BCMNFC_SET_WAKE_ACTIVE_STATE, 1);
        return 0;
    }

    if (!strcmp(mode, "--reset")) {
        printf("== reset sequence ==\n");
        do_reset();
        printf("done.\n");
        return 0;
    }

    if (!strcmp(mode, "--send") && argc > 2) {
        unsigned char out[64];
        int n = 0;
        for (const char *p = argv[2]; *p && n < 64; ) {
            while (*p == ' ' || *p == ',') p++;
            if (!*p) break;
            char *end;
            long v = strtol(p, &end, 16);
            if (end == p) break;
            out[n++] = (unsigned char)v;
            p = end;
        }
        printf("== send %d bytes, then read the reply ==\n", n);
        do_reset();
        int r = wait_for(0x00, MT_NTF, 5000, "core reset ntf");
        if (r == 0 && write_all(out, n) == 0)
            wait_for(out[2] & 0x3F, MT_RSP, 5000, "reply");
        return 0;
    }

    if (!strcmp(mode, "--bringup") || !strcmp(mode, "--poke")) {
        printf("== BRINGUP ==\n\n");
        printf("step 1: reset (wake off -> power off -> 10ms -> power on)\n");
        do_reset();

        printf("\nstep 2: wait for the chip's Core Reset notification\n");
        if (wait_for(0x00, MT_NTF, 8000, "core reset ntf") != 0) {
            printf("\nno notification. The chip did not come up.\n");
            return 1;
        }

        printf("\nstep 3: send GET_BUILD_INFO (10 2F 04 00)\n");
        if (write_all(CMD_GET_BUILD_INFO, sizeof CMD_GET_BUILD_INFO) != 0)
            return 1;

        printf("\nstep 4: read the response\n");
        int r = wait_for(0x04, MT_RSP, 8000, "reply to GET_BUILD_INFO");
        printf("\n-- bringup %s --\n",
               r == 0 ? "REACHED THE RESPONSE" : "did not get a response");
        if (r != 0)
            printf("   Next step is nfc_hal_dm_send_startup_vsc() / the patchram\n"
                   "   sequence in nfc_hal_prm.c, not a transport problem.\n");
        return 0;
    }

    /* --listen */
    printf("== listen %ds, chip powered, nothing sent ==\n", secs);
    do_reset();
    int packets = 0;
    for (int e = 0; e < secs; e++) {
        int n = read_packet(buf, sizeof buf, 1000);
        if (n < 0) { printf("  read error: %s\n", strerror(errno)); break; }
        if (n == 0) { putchar('.'); fflush(stdout); continue; }
        putchar('\n');
        char tag[16];
        snprintf(tag, sizeof tag, "[%d]", ++packets);
        show(tag, buf, n);
    }
    printf("\n== %d packet(s) in %ds ==\n", packets, secs);
    return 0;
}
