/*
 * init-stub.c -- the hammerhead's init: TWRP, or our firmware.
 *
 * The device boots the TWRP ramdisk.  This stub stands in for TWRP's
 * own init as PID 1:
 *
 * The stub is *not* the init: TWRP's own init stays PID 1, so the
 * mounts, the USB gadget and adb all come up exactly as they always
 * did.  The stub is started as a service (at the end of init.rc, so
 * after adbd), and then:
 *
 *   - if the volume up key is NOT held, the firmware is started: the
 *     recovery's user interface is stopped and the firmware takes the
 *     display and the touch screen.  This is the default.
 *   - if the volume up key IS held, nothing happens and the device
 *     is a perfectly ordinary TWRP -- and there is no sign that the
 *     firmware is on it at all.
 *
 * So the firmware is the default payload and the volume up key is the
 * way back to TWRP.
 *
 * The key is read from the input device (the gpio keys), because the
 * state of a key that is already held is not re-reported when the
 * device is opened: the stub waits for the press.
 *
 * It is built static (the ramdisk has no libc of its own that is
 * worth relying on) with the armel toolchain, the same one the
 * firmware's MicroPython is built with.
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdlib.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* the gpio keys: the power key and the two volume keys */
#define KEYS_NODE "/dev/input/event2"

/* how long to wait for the key, in milliseconds */
#define KEY_WINDOW_MS 4000

/* how long to wait for adb before touching anything, in seconds */
#define USB_WAIT_S 8

/* Where the firmware lives.
 *
 * During development it is on the data partition, so that the ramdisk
 * stays small and the firmware can be updated over adb without
 * repacking the boot image.  A production image can instead carry a
 * read-only copy in the ramdisk at /opt/krux; that copy wins when it is
 * present, and the data copy remains the development fallback. */
#define FIRMWARE_DIR "/data/krux"
#define BUNDLED_FIRMWARE_DIR "/opt/krux"

/* the firmware's own log: init gives a service /dev/null on stdout and
 * stderr, so without this the firmware's messages -- how far it got, and
 * why it stopped -- are lost. */
#define FIRMWARE_LOG "/data/krux/fw.log"

/* The firmware's settings store writes to the absolute paths "/flash/"
 * and "/sd/" (krux/settings.py: Store.get_vfs_location returns
 * "/" + location + "/", so the working directory does not matter).  This
 * device has neither: there is no SPI flash and no SD card.  Both are
 * therefore pointed at the data partition, so the firmware can read and
 * write its settings, and so that a TC code file ("/flash/tcc") can be
 * put there. */
#define SETTINGS_DIR "/data/krux/flash"
#define SD_DIR "/data/krux/sd"

/* the camera daemon and its socket */
#define CAMD_SOCKET "/data/krux/camd.sock"
#define CAMD_LOG "/data/krux/camd.log"

/* TWRP's own user interface, which we keep for the volume up key */
#define RECOVERY_BIN "/sbin/recovery.stock"

static void say(const char *what) {
    fprintf(stderr, "init-stub: %s\n", what);
}

/* Is the volume up key pressed?  The press is an event, so the stub
 * waits for it; the key state ioctl only covers the first 32 key
 * codes, and the volume keys are above that.
 *
 * The window is short: the key has to be held while the device comes
 * up, which is when the service starts. */
static int wait_for_the_key(int window_ms) {
    struct pollfd pfd;
    int fd;
    int elapsed = 0;

    fd = open(KEYS_NODE, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        say("no key device");
        return 0;
    }

    while (elapsed < window_ms) {
        struct input_event ev;
        ssize_t n;

        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        if (poll(&pfd, 1, 200) > 0 && (pfd.revents & POLLIN)) {
            n = read(fd, &ev, sizeof(ev));
            if (n == (ssize_t)sizeof(ev)) {
                if (ev.type == EV_KEY && ev.code == KEY_VOLUMEUP && ev.value != 0) {
                    say("the volume up key is held: TWRP it is");
                    close(fd);
                    return 1;
                }
            }
        }
        elapsed += 200;
    }
    close(fd);
    return 0;
}

/* Start TWRP's own user interface (the volume up key). */
static void *start_twrp(void *arg) {
    (void)arg;
    say("starting TWRP");
    execl(RECOVERY_BIN, "recovery", (char *)NULL);
    say("could not start TWRP");
    return NULL;
}

/* The firmware's settings store wants "/flash" and "/sd".  Point them at
 * the data partition. */
static void make_the_settings_directories(void) {
    /* bundled firmware still keeps its mutable settings on /data */
    mkdir("/data/krux", 0755);
    mkdir(SETTINGS_DIR, 0755);
    mkdir(SD_DIR, 0755);
    if (symlink(SETTINGS_DIR, "/flash") != 0 && errno != EEXIST) {
        say("could not make /flash");
    }
    if (symlink(SD_DIR, "/sd") != 0 && errno != EEXIST) {
        say("could not make /sd");
    }
}

/* Give the firmware a log file: init hands a service /dev/null, so
 * without this every message the firmware prints is lost. */
static void redirect_the_output(void) {
    int fd = open(FIRMWARE_LOG, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (fd < 0) {
        say("could not open the firmware's log");
        return;
    }
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    if (fd > STDERR_FILENO) {
        close(fd);
    }
}

/* Start the camera daemon before the firmware.  It owns the V4L2/ISP
 * state machine; the MicroPython sensor module is only its socket client.
 * The fallback configuration is the CAMIF_RAW + CPP path that was
 * validated on this device. */
static void start_camd(const char *dir) {
    char camd[PATH_MAX];
    char fw[PATH_MAX];
    pid_t pid;
    int i;

    snprintf(camd, sizeof(camd), "%s/camd", dir);
    if (access(camd, X_OK) != 0) {
        say("no camd next to the firmware");
        return;
    }
    snprintf(fw, sizeof(fw), "%s/cpp_firmware_v1_2_0.fw", dir);
    if (access(fw, R_OK) != 0) {
        snprintf(fw, sizeof(fw), "/lib/firmware/cpp_firmware_v1_2_0.fw");
    }

    unlink(CAMD_SOCKET);
    pid = fork();
    if (pid == 0) {
        int fd;
        setsid();
        if (chdir(dir) != 0) {
            /* keep going: the paths passed below are absolute */
        }
        fd = open(CAMD_LOG, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > STDERR_FILENO) {
                close(fd);
            }
        }
        execl(camd, "camd",
              "1640", "1232", "0x40000000", "rear", "0",
              fw, "2", "binned", "cfg", "nocycle",
              "0", "0x2b", "0", "0", "300",
              "0", "0", "0", "0x12", "0", "1",
              "0xffffffff", "0", "0", "0", "1", "0",
              (char *)NULL);
        _exit(127);
    }
    if (pid < 0) {
        say("could not fork camd");
        return;
    }

    /* The firmware's sensor client retries, but waiting here keeps the
     * first "Loading Camera..." from spending its whole budget on a
     * daemon that is still opening the ISP. */
    for (i = 0; i < 80; i++) {
        if (access(CAMD_SOCKET, F_OK) == 0) {
            say("camd is ready");
            return;
        }
        usleep(100000);
    }
    say("camd did not create its socket");
}

static void *start_the_firmware(void *arg) {
    (void)arg;
    /* the panel: TWRP's screen saver may have dimmed it, and the
     * firmware draws on it.
     *
     * HALF the maximum, not all of it: at full brightness the panel
     * runs hot enough to be uncomfortable to hold, and the device is
     * meant to be carried.  The maximum is read from the driver
     * rather than assumed, so this follows the hardware. */
    {
        FILE *f = fopen("/sys/class/backlight/lm3630/brightness", "w");

        if (f != NULL) {
            int maximum = 255;
            FILE *m = fopen("/sys/class/backlight/lm3630/max_brightness", "r");

            if (m != NULL) {
                if (fscanf(m, "%d", &maximum) != 1 || maximum <= 0) {
                    maximum = 255;
                }
                fclose(m);
            }
            fprintf(f, "%d", maximum / 2);
            fclose(f);
        }
    }
    /* Stop TWRP's user interface.  It is a service of TWRP's init,
     * and this is how a service is stopped.
     *
     * The service is called "recovery" and *this* program is not it:
     * the stub runs as the service "krux" (see
     * init.recovery.service.rc).  When the stub was the service
     * "recovery", this stop killed the stub's own process group -- and
     * with it the firmware the stub had just started -- and init
     * restarted the service, and the loop blanked the panel every
     * 140 ms.
     *
     * The absolute path: a service's PATH is not the init's. */
    if (system("/sbin/setprop ctl.stop recovery") != 0) {
        say("could not stop the recovery");
    }
    sleep(2);
    make_the_settings_directories();
    redirect_the_output();

    /* Prefer the read-only copy bundled in the boot image, if there is
     * one.  The development copy on /data stays as the fallback so the
     * same stub works with either image. */
    {
        const char *dir = FIRMWARE_DIR;
        char binary[64];
        char main_py[64];

        if (access(BUNDLED_FIRMWARE_DIR "/micropython", X_OK) == 0 &&
            access(BUNDLED_FIRMWARE_DIR "/boot.py", R_OK) == 0) {
            dir = BUNDLED_FIRMWARE_DIR;
            say("using the firmware bundled in the boot image");
        } else {
            say("using the firmware on the data partition");
        }
        snprintf(binary, sizeof(binary), "%s/micropython", dir);
        snprintf(main_py, sizeof(main_py), "%s/boot.py", dir);
        start_camd(dir);
        if (chdir(dir) != 0) {
            say("no firmware directory");
        }
        say("starting the firmware");
        execl(binary, "micropython", main_py, (char *)NULL);
    }
    say("the firmware did not start");
    return NULL;
}

/* Mount the data partition: TWRP's own binary does that, and the
 * firmware lives there. */
static void mount_the_data(void) {
    struct stat st;

    if (stat("/data", &st) == 0 && S_ISDIR(st.st_mode)) {
        /* it may be there but not mounted */
    }
    mkdir("/data", 0755);
    if (mount("/dev/block/bootdevice/by-name/userdata", "/data", "ext4", 0, NULL) != 0) {
        say("could not mount /data");
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    /* let the USB gadget and adb come up first: the device has to be
     * debuggable even if the firmware goes wrong */
    sleep(USB_WAIT_S);

    mount_the_data();

    if (wait_for_the_key(KEY_WINDOW_MS)) {
        /* the volume up key: TWRP, as it always was */
        start_twrp(NULL);
        return 0;
    }

    /* the default: our firmware */
    start_the_firmware(NULL);
    return 0;
}
