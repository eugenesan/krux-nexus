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
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>

/* the gpio keys: the power key and the two volume keys */
#define KEYS_NODE "/dev/input/event2"

/* how long to wait for the key, in milliseconds */
#define KEY_WINDOW_MS 8000

/* how long to wait for adb before touching anything, in seconds */
#ifdef KRUX_SEAL
#define USB_WAIT_S 0
#endif
#define USB_WAIT_S 8

/* Where the firmware lives.
 *
 * The development copy is on the data partition, so that the ramdisk
 * stays small and the firmware can be updated over adb without
 * repacking the boot image.  scripts/deploy-firmware.sh pushes the
 * project tree there, and it is the ONLY place a code change is ever
 * made: the files on the device are a deployment artifact, never the
 * source.
 *
 * A production image can instead carry a read-only copy in the ramdisk
 * at /opt/krux.  That copy is the FALLBACK: it is used only when the
 * data partition has no firmware tree of its own.  It must not take
 * precedence, or a push to /data/krux would silently have no effect
 * and the device would keep running the code baked into the image. */
#define FIRMWARE_DIR "/data/krux"
#define BUNDLED_FIRMWARE_DIR "/opt/krux"

/* the firmware's own log: init gives a service /dev/null on stdout and
 * stderr, so without this the firmware's messages -- how far it got, and
 * why it stopped -- are lost. */
#ifdef KRUX_RELEASE
/* A delivered image writes no log: the firmware's and camd's output go
 * to /dev/null, and the stub itself is silent. */
#define FIRMWARE_LOG "/dev/null"
#else
#define FIRMWARE_LOG "/data/krux/fw.log"
#endif

/* The firmware's settings store writes to the absolute paths "/flash/"
 * and "/sd/" (krux/settings.py: Store.get_vfs_location returns
 * "/" + location + "/", so the working directory does not matter).  This
 * device has neither: there is no SPI flash and no SD card.  Both are
 * therefore pointed at the data partition, so the firmware can read and
 * write its settings, and so that a TC code file ("/flash/tcc") can be
 * put there. */
#ifdef KRUX_RELEASE
/* The release keeps no settings or backups on the data partition: the
 * firmware's /flash and /sd are directories in the initramfs, so they
 * are gone on the next power-off. */
#define SETTINGS_DIR "/flash"
#define SD_DIR "/sd"
#else
#define SETTINGS_DIR "/data/krux/flash"
#define SD_DIR "/data/krux/sd"
#endif

/* the camera daemon and its socket */
#define CAMD_SOCKET "/data/krux/camd.sock"
#ifdef KRUX_RELEASE
#define CAMD_LOG "/dev/null"
#else
#define CAMD_LOG "/data/krux/camd.log"
#endif

/* TWRP's own user interface, which we keep for the volume up key */
#define RECOVERY_BIN "/sbin/recovery.stock"

/* The delivery mode.
 *
 * The development image starts the firmware at boot: the ramdisk is
 * small, the firmware lives on the data partition and a push over adb
 * is all it takes to change it.
 *
 * The standard image does not.  It carries the firmware read-only in
 * the ramdisk, leaves the data partition for settings only, and waits
 * for the volume up key -- or for a script to ask it -- before it runs
 * anything of its own.  The mode is chosen when the stub is built, and
 * the data partition can override it, so one image can be tried both
 * ways: a file with a 1 there means "start the firmware at boot", a 0
 * means "wait to be asked". */
#define AUTOSTART_FILE "/data/krux/state/autostart"

/* True when the image is a standard one: the firmware is read-only in
 * the boot image and the data partition holds the settings only. */
static int standard_image(void) {
#ifdef DEFAULT_STANDARD
    return 1;
#else
    return 0;
#endif
}

/* True when the firmware is started at boot; the standard image says
 * no unless the data partition says otherwise. */
static int autostart_the_firmware(void) {
    int autostart = !standard_image();
#ifndef KRUX_RELEASE
    /*
     * A development knob: /data/krux/state/autostart overrides the
     * image default so the bench can boot straight into the firmware
     * without a key.  The release does not read the data partition at
     * all, and the standard image must come up with TWRP.
     */
    FILE *f = fopen(AUTOSTART_FILE, "r");

    if (f != NULL) {
        int v = -1;

        if (fscanf(f, "%d", &v) == 1 && (v == 0 || v == 1)) {
            autostart = v;
        }
        fclose(f);
    }
#endif
    return autostart;
}

/* Run a program and wait for it.
 *
 * There is no /bin/sh in this ramdisk -- only /sbin/sh -- so system()
 * cannot be used: it fails every time, and the program it was meant
 * to run never happens. */
static int run_program(const char *path, char *const argv[]) {
    pid_t pid;
    int status = -1;

    pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        execv(path, argv);
        _exit(127);
    }
    if (waitpid(pid, &status, 0) == pid && WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

/* Stop an init service by name.
 *
 * "setprop ctl.stop <service>" is two arguments: the property is
 * "ctl.stop" and the value is the service's name.  init watches the
 * property and stops the service. */
static int stop_service(const char *name) {
    char *argv[4];

    argv[0] = (char *)"setprop";
    argv[1] = (char *)"ctl.stop";
    argv[2] = (char *)name;
    argv[3] = NULL;
    return run_program("/sbin/setprop", argv);
}

static void say(const char *what) {
#ifdef KRUX_RELEASE
    (void)what;
#else
    fprintf(stderr, "init-stub: %s\n", what);
#endif
}

/* Is the volume up key pressed?  The press is an event, so the stub
 * waits for it; the key state ioctl only covers the first 32 key
 * codes, and the volume keys are above that.
 *
 * The window is short: the key has to be held while the device comes
 * up, which is when the service starts. */
/* Is the volume up key held down right now?
 *
 * The event is only reported when the key is *pressed*, so a key that
 * was already held when the device came up would never be seen.  The
 * ioctl that reports the current key state does see it: it is a bitmap
 * of every key code the driver knows, and the volume keys are in it. */
static int the_key_is_down(int fd) {
    unsigned char state[128];

    memset(state, 0, sizeof(state));
    if (ioctl(fd, EVIOCGKEY(sizeof(state)), state) < 0) {
        return 0;
    }
    /* the bitmap is indexed by the key code: bit 0 is KEY_RESERVED */
    if (KEY_VOLUMEUP / 8 >= (int)sizeof(state)) {
        return 0;
    }
    return (state[KEY_VOLUMEUP / 8] >> (KEY_VOLUMEUP % 8)) & 1;
}

static int wait_for_the_key(int window_ms) {
    struct pollfd pfd;
    int fd;
    int elapsed = 0;

    fd = open(KEYS_NODE, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        say("no key device");
        return 0;
    }

    /* already held?  Then it was pressed before this program started
     * and no event is coming. */
    if (the_key_is_down(fd)) {
        say("the volume up key is already held");
        close(fd);
        return 1;
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
                    say("the volume up key is held");
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
#ifdef KRUX_RELEASE
    /* /flash and /sd are ordinary directories in the initramfs */
    mkdir(SETTINGS_DIR, 0700);
    mkdir(SD_DIR, 0700);
#else
    /* development keeps its mutable settings on /data */
    mkdir("/data/krux", 0755);
    mkdir(SETTINGS_DIR, 0755);
    mkdir(SD_DIR, 0755);
    if (symlink(SETTINGS_DIR, "/flash") != 0 && errno != EEXIST) {
        say("could not make /flash");
    }
    if (symlink(SD_DIR, "/sd") != 0 && errno != EEXIST) {
        say("could not make /sd");
    }
#endif
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
        /* Supervisor: sensor.run(0) sends Q, camd exits, and the next
         * camera session gets a fresh process (and therefore a fresh
         * CPP queue) instead of a dark, stale preview. */
        for (;;) {
            pid_t c = fork();
            if (c == 0) {
                execl(camd, "camd",
                      "1640", "1232", "0x40000000", "rear", "0",
                      fw, "2", "binned", "cfg", "nocycle",
                      "0", "0x2b", "0", "0", "300",
                      "0", "0", "0", "0x12", "0", "1",
                      "0xffffffff", "0", "0", "0", "1", "0",
                      (char *)NULL);
                _exit(127);
            }
            if (c > 0) {
                int status;
                waitpid(c, &status, 0);
            }
            sleep(1);
        }
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
    if (stop_service("recovery") != 0) {
        say("could not stop the recovery");
    }

    sleep(2);
    make_the_settings_directories();
    redirect_the_output();

    /* The development copy on the data partition wins: it is what
     * deploy-firmware.sh pushes, so it is the only tree whose code
     * changes ever take effect.  The read-only copy bundled in the
     * boot image is the fallback for a production image with no
     * development tree on the data partition. */
    {
        const char *dir;
        char binary[64];
        char main_py[64];
        int bundled_copy = 0;
        int on_data = access(FIRMWARE_DIR "/micropython", X_OK) == 0 &&
                      access(FIRMWARE_DIR "/boot.py", R_OK) == 0;
        int bundled = access(BUNDLED_FIRMWARE_DIR "/micropython", X_OK) == 0 &&
                      access(BUNDLED_FIRMWARE_DIR "/boot.py", R_OK) == 0;

        if (standard_image() && bundled) {
            /* The standard image: the read-only copy in the boot
             * image is the firmware; the data partition holds the
             * settings and nothing else. */
            dir = BUNDLED_FIRMWARE_DIR;
            bundled_copy = 1;
            say("using the firmware bundled in the boot image");
        } else if (on_data) {
            /* The development copy on the data partition wins: it is
             * what deploy-firmware.sh pushes, so it is the only tree
             * whose code changes ever take effect. */
            dir = FIRMWARE_DIR;
            say("using the firmware on the data partition");
        } else if (bundled) {
            /* No development tree, but the image carries one. */
            dir = BUNDLED_FIRMWARE_DIR;
            bundled_copy = 1;
            say("using the firmware bundled in the boot image");
        } else {
            say("no firmware: neither the data partition nor the boot "
                "image carries one");
            dir = FIRMWARE_DIR;
        }
        snprintf(binary, sizeof(binary), "%s/micropython", dir);
        snprintf(main_py, sizeof(main_py), "%s/boot.py", dir);
        start_camd(dir);
        /* The camera kernel can dump the first 64 CPP frames to
         * /data/local/tmp for protocol analysis.  That dump drops ISP
         * frames, keeps the CPU busy and makes the camera pipeline
         * stall with empty buffers.  It is a diagnostic hook, not
         * normal operation; turn it off now that the CPP node has
         * probed. */
        {
            FILE *f = fopen(
                "/sys/module/msm_cpp/parameters/cpp_dump_frames", "w");
            if (f != NULL) {
                fputs("0", f);
                fclose(f);
            }
        }
        if (chdir(dir) != 0) {
            say("no firmware directory");
        }
        /* The firmware needs to know which delivery mode it runs in:
         * a bundled device is an appliance and may power itself off
         * when it is left alone; a development one is a development
         * target and may not. */
        setenv("KRUX_DELIVERY",
               bundled_copy ? "bundled" : "development", 1);
        say(bundled_copy ? "delivery mode: bundled"
                         : "delivery mode: development");
        say("starting the firmware");
        /* The unix port's default GC heap is 1 MiB.  Krux's full UI,
         * the 720x960 luma frame (691,200 bytes), a 720x960 RGB565
         * preview (1,382,400 bytes) and the QR decoder do not fit in
         * 8 MiB; use 32 MiB, which is still small for this device. */
        execl(binary, "micropython", "-X", "heapsize=32M", main_py,
              (char *)NULL);
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
    if (mount("/dev/block/bootdevice/by-name/userdata", "/data", "ext4", 0, NULL) != 0 &&
        errno != EBUSY) {
        /* EBUSY means it is already mounted, which is the normal case
         * when TWRP has mounted it for us. */
        say("could not mount /data");
    }
}

int main(int argc, char **argv) {
    (void)argc;

    /* The environment is what the Python and camd read to choose the
     * release behaviour.  A missing variable means release on the
     * Python side; the stub sets it explicitly for both. */
#ifdef KRUX_RELEASE
    setenv("KRUX_RELEASE", "1", 1);
#else
    setenv("KRUX_RELEASE", "0", 1);
#endif
#ifndef KRUX_RELEASE
    /* the bench keeps the frame dumps for its tests */
    setenv("CAMD_ALLOW_DUMPS", "1", 1);
#endif
    {
        struct rlimit no_core = { 0, 0 };
        setrlimit(RLIMIT_CORE, &no_core);
    }

#ifdef KRUX_SEAL
    /* Keep the audit channel off in every path, TWRP included.  TWRP's
     * init restarts adbd on sys.usb.config=adb, so stopping the service
     * once is not enough: hide the binary as well.  The operator brings
     * it back with /sbin/start-adb when adb is needed (for example to
     * sideload an Android update). */
    if (rename("/sbin/adbd", "/sbin/adbd.off") != 0 && errno != ENOENT) {
        say("could not hide adbd");
    }
    stop_service("adbd");
#endif

    /* A client: "krux-init start" starts the firmware on a running
     * image.  It is how the standard image is started from adb or from
     * TWRP's terminal, without a reboot. */
    if (argc > 1 && strcmp(argv[1], "start") == 0) {
        /* TWRP is the service this stub became, so it is the service
         * to stop; the two would otherwise fight over the panel. */
        if (stop_service("krux") != 0) {
            say("could not stop TWRP");
        }
        sleep(2);
    #ifndef KRUX_RELEASE
    mount_the_data();
#endif
        start_the_firmware(NULL);
        return 0;
    }

    /* let the USB gadget and adb come up first: the device has to be
     * debuggable even if the firmware goes wrong */
    sleep(USB_WAIT_S);

    mount_the_data();

    if (autostart_the_firmware()) {
        /* the development image: without a key the firmware starts,
         * and the volume up key is TWRP, as it always was */
        if (wait_for_the_key(KEY_WINDOW_MS)) {
            start_twrp(NULL);
            return 0;
        }
        start_the_firmware(NULL);
        return 0;
    }

    /* The standard image: the device comes up with TWRP, and the
     * firmware is started on request -- the volume up key during this
     * window, or a script (see /sbin/start-krux). */
    if (wait_for_the_key(KEY_WINDOW_MS)) {
        say("the volume up key is held: the firmware it is");
        start_the_firmware(NULL);
        return 0;
    }
    say("no key: TWRP it is.  Hold the volume up key while it boots, "
        "or run /sbin/start-krux, for the firmware.");
    start_twrp(NULL);
    return 0;
}
