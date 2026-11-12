# BUILD-GUIDE.md -- how to build the firmware from scratch

This is the shortest path from a fresh checkout to a running Krux
firmware on the hammerhead (the LG Nexus 5).

Everything here is in this repository; the only things that are not
are the two upstream trees (the kernel and the firmware's own source)
and the toolchain, and those are fetched or unpacked by the scripts
below.

The **delivery modes** are the thing to understand first, because they
decide which image you build and what runs when the device comes up.

| mode | the firmware lives | what starts at boot | how the firmware is started |
|---|---|---|---|
| **development** (the default) | `/data/krux`, pushed over adb | the firmware, automatically | -- |
| **standard** | `/opt/krux`, read-only in the boot image | **nothing**: the device comes up in TWRP | the volume-up key during the boot, or `/sbin/start-krux` |
| **extreme** | a minimal initramfs, no partitions | the firmware | -- (deferred: it needs UART) |

The plan for all three, and what was measured, is in
[packaging-plan.md](packaging-plan.md).

## What has to be built

| what | how | where it ends up |
|---|---|---|
| the **init stub** | `rootfs/build-stub-init.sh` | `rootfs/stub/krux-init` |
| the **stub ramdisk** | `rootfs/build-stub-ramdisk.sh` | `artifacts/built/ramdisk-stub` |
| the **boot image** | `rootfs/make-boot-image.sh` | `artifacts/built/boot-stub.img` |
| the **firmware** (the MicroPython interpreter) | `scripts/build/build-micropython.sh` | `rootfs/micropython-arm32` |
| the **camera daemon** | `scripts/cam/build-camd.sh` | `cam/camd` |
| the **kernel** | `tools/build-kernel.sh` | the output directory you name |
| the **deploy** | `scripts/deploy-firmware.sh` | on the device, in `/data/krux` |

Build outputs under `artifacts/built/` are **gitignored**; do not check
them in, and do not write ramdisks or images under `rootfs/`.

The boot image is only needed when the **kernel** changes; during
development the firmware is deployed over adb and does not need one.

## 0. The tools

* `arm-linux-gnueabihf-gcc` (the host's cross compiler) -- for the
  firmware, the camera daemon and the stub.
* `python3`, `cpio`, `gzip`, `xz`, `git`, `make`, `bc`, `flex`,
  `bison` -- for the builds.
* `adb` and `fastboot` -- to talk to the device.
* `dtc` -- to look at the device trees (optional).

**The toolchain the kernel is built with** is the host's
`arm-linux-gnueabihf-gcc`, the same one the firmware, camd and the
stub use.  `tools/build-kernel.sh` selects it by default
(`TOOLCHAIN=host`); nothing extra needs to be fetched.

(The ROM's own kernels were built with Arch's `arm-none-eabi-gcc`
16.2.0.  We tried that toolchain for a few experiments and it stuck in
these notes for a while; it is **not** what this project builds with
and `TOOLCHAIN=arch` is not needed.)

**The sysroot** is the host's `/usr/arm-linux-gnueabihf` (the
`arm-linux-gnueabihf` cross compiler's own headers and static libc).
Nothing needs to be assembled.

The Debian soft-float `armel-sysroot` (`scripts/build-armel-sysroot.sh`)
and the Linaro / ARM GNU (`arm-none-eabi`) toolchains are **obsolete**.
They were used for experiments and are kept only for the history in
`docs/toolchain.md`; every component is built with the host's
`arm-linux-gnueabihf`.

## 1. The kernel

```sh
bash tools/build-kernel.sh --tree linux --out /tmp/opencode/ko
```

That takes about fifteen minutes on four cores (the tree is a CAF
3.4 tree with about 2,500 translation units) and leaves:

```
/tmp/opencode/ko/arch/arm/boot/zImage-dtb
```

**Use `zImage-dtb`, not `zImage`.**  The bootloader finds the device
tree by scanning the kernel image for it, and a plain compressed
`zImage` has none: the bootloader answers "dtb not found" and
refuses to boot.  (`zImage-dtb` is the compressed kernel with the
device trees appended.)

The build applies the air-gap profile
(`tools/profiles/hammerhead.list`), which switches the
radio off, and verifies afterwards that nothing turned it back on.

## 2. The init stub and the ramdisk

```sh
bash rootfs/build-stub-init.sh                 # the development stub
bash rootfs/build-stub-ramdisk.sh
```

The stub is the program that stands in for TWRP's own `init`: it
waits for adb, waits for the volume-up key, sets the backlight to
half, and then hands the display and the touch screen to the firmware.

The ramdisk build takes three flags:

| flag | what it does |
|---|---|
| `--force` | replace the ramdisk that is already there.  Without it, a build whose *content* differs from the existing ramdisk stops and says so; the cpio is not byte-reproducible (it carries inode numbers and mtimes), so the content is what is compared |
| `--bundle` | copy MicroPython, Krux and embit into `/opt/krux` in the ramdisk: the boot image carries a read-only firmware |
| `--standard` | build the stub for the **standard** delivery mode (see below) |

and the environment variables `COMPRESS` (`gzip`, the default; `xz`,
`lzma`, `lz4`), `KEEP_KEYSTORE=1` (keep TWRP's keystore binaries) and
`OUT`.

The two modes differ in what the stub does when the device comes up:

* **development** (the default): no key -- the firmware starts; the
  volume-up key -- TWRP.  The firmware is the copy on `/data/krux`, so
  a push over adb takes effect.
* **standard** (`--standard`): no key -- TWRP; the volume-up key --
  the firmware, running the read-only copy in `/opt/krux`.  Nothing
  starts by itself, and the data partition holds the settings only.

The mode can be overridden at run time by a `1` in
`/data/krux/state/autostart` (see *the knobs* below).

The ramdisk build prunes what the stub does not use: TWRP's
`terminfo` and Python 3.10 trees, the UBSan runtime (no binary has a
`DT_NEEDED` on it), and the keystore helpers.  The CPP firmware is kept
only in `/lib/firmware`; `/opt/krux` no longer carries a duplicate.
TWRP's `adbd`, linker, libc and libdexfile are stripped (the dynamic
symbols stay).  The init stub and camd are built with `-Os -s` (camd
also uses `-ffunction-sections -fdata-sections -Wl,--gc-sections`).

**Compression.**  The default is **gzip**, and that is what a
*flashed* image needs: the stock bootloader looks for a gzip ramdisk
and does not look for anything else, so an XZ ramdisk there boots
nothing, silently.  On the `fastboot boot` path the *kernel* unpacks
the ramdisk, and the kernel has `CONFIG_RD_XZ=y`
(`lineageos_hammerhead_defconfig`), so `COMPRESS=xz` works there and
makes the ramdisk 8.7 MB against 14.5 MB.  Use it for the images this
project boots; keep gzip for anything that may be flashed.

## 3. The boot image

```sh
bash rootfs/make-boot-image.sh \
    artifacts/twrp-3.7.0_9-HH.R.17-MADV_WIPEONFORK.img \
    /tmp/opencode/ko/arch/arm/boot/zImage-dtb \
    artifacts/built/boot-stub.img
```

The template is a boot image that is known to boot on this device;
the script takes its header (the load addresses, the page size, the
command line) and swaps in our kernel and our ramdisk.  **Do not
invent those numbers**: the stock bootloader loads the kernel and the
ramdisk at exactly the addresses in the header, and getting them
wrong produces a device that boots to nothing at all.

The ramdisk it packs is `artifacts/built/ramdisk-stub`, or whatever
`RAMDISK=` names.  The script fails rather than emitting an image
that cannot be flashed.

## 4. The firmware

```sh
bash scripts/build/build-micropython.sh
```

About three minutes.  It builds the MicroPython checkout in
`krux/firmware/micropython` at HEAD (unix port, 1.30.0-dev) with the port's
own modules -- the display, the input devices, the NFC, the I2C bus and the
rest of the `machine` module -- and leaves `rootfs/micropython-arm32`.

The source tree is the nested `krux/firmware/micropython` submodule. The
engine's `build-standard` directory and object files are gitignored and
disposable; no separate source worktree is created. The cUR sources are the
nested `krux/firmware/micropython/lib/cUR` submodule and are staged only for
the duration of the build.

The camera daemon is built separately:

```sh
bash scripts/cam/build-camd.sh
```

## 5. Deploy

```sh
bash scripts/deploy-firmware.sh
```

That pushes the interpreter, the Krux package, the port's own modules,
embit and the camera daemon to the device, clears the byte-code cache
and restarts the firmware (`setprop ctl.stop krux` /
`setprop ctl.start krux`).  The firmware's log is
`/data/krux/fw.log`, the daemon's is `/data/krux/camd.log`.

It also removes the `.py` files on the device that the project tree
does not have (a stray file would shadow the tree, because the
firmware imports by name) and rebuilds `camd` when `cam/camd.c` is
newer than the binary, so the daemon can never drift from its source.

## 5b. The standard image: read-only firmware, nothing starts by itself

```sh
bash scripts/build/build-micropython.sh
bash rootfs/build-stub-ramdisk.sh --standard --bundle --force
RAMDISK=artifacts/built/ramdisk-bundled \
    bash rootfs/make-boot-image.sh \
    artifacts/twrp-3.7.0_9-HH.R.17-MADV_WIPEONFORK.img \
    /tmp/opencode/ko/arch/arm/boot/zImage-dtb \
    artifacts/built/boot-standard.img
```

The image boots into **TWRP 3.7.0** (the menu draws, `/data` is
mounted, adb works), and the firmware waits to be asked:

* **the volume-up key**, held while the device boots.  The stub reads
  it as a key *state*, not only as an event, so a key that was already
  held when the device came up counts too.
* **`/sbin/start-krux`**, from adb or from TWRP's own terminal:

  ```sh
  adb shell sh /sbin/start-krux
  ```

  It stops TWRP and starts the firmware, running the read-only copy in
  `/opt/krux`.  The data partition keeps the settings and the logs.

`krux-init start` is what the script calls; it is the same program as
the stub, with an argument.

Two behaviours of the standard mode are worth knowing before an image
is given to somebody else:

* **the wipe empties the data partition.**  "Erase User's Data" removes
  the settings, the encrypted mnemonics *and* the deployment, so the
  device comes up with the same firmware and no state.  That is only
  safe because the firmware is in the boot image.
* **the device powers itself off when it is left alone.**  The idle
  timeout calls the firmware's own shutdown -- in the bundled mode
  only; a development device stays alive and reachable over adb.

## 6. Boot it

The images are **not flashed** -- they are booted, so the phone can
always be brought back with its own boot partition:

```sh
adb reboot bootloader
fastboot boot artifacts/built/boot-stub.img
```

(If the device is in the ROM's Android, `adb reboot bootloader`
works; from fastboot, `fastboot boot` works.)

## The knobs

Two places, and the difference matters:

| | what is in it | when it is read |
|---|---|---|
| `/data/krux/state/` | the **persistent** knobs: `stream`, `stream_idle`, `mode`, `cycle`, `capture_cfg`, `fov`, `profile`, `autostart` | at every start of the daemon |
| `/tmp/krux/` | the **volatile** ones: `af`, `focus`, `luma_gain`, `unsharp` | at every start; `/tmp` is a tmpfs, so they are gone after a reboot |

| knob | what it does |
|---|---|
| `stream` | 0 = the raw Bayer through the CPP, 1 = the vendor's processed preview (PIX_VIEWFINDER).  1 is the default |
| `stream_idle` | 1 = stop the sensor when no page has asked for a frame for about 3 s, and start it again on the next request.  0 = stream continuously.  1 is the default |
| `mode` | 0 = the binned 1640x1232 sensor mode, 1 = the full 3280x2464 one.  The processed preview forces the full mode |
| `cycle` | the vendor's per-frame ISP cycle (on by default) |
| `capture_cfg` | the vendor's per-capture ISP configuration (off by default) |
| `fov` | the window: 0 = a 2160 square in the centre, 1 = the whole frame |
| `autostart` | 1 = start the firmware at boot even in the standard image |

**The stream is on demand.**  `camd` stops the sensor (the same pause
the autofocus makes around a lens move) after about 3 s with no frame
request, and the first request starts it again.  The nodes, buffers
and ISP/CPP configuration all stay as they are -- this is **not**
`STREAMOFF`.  While paused the ISP, the CPP and the per-frame kernel
work stop, which is what makes an idle device cool; measured on the
device, the system CPU time fell from ~38 % to ~6 %.

Consequence for restarts: **stop the service only while the stream is
paused.**  Killing `camd` while the sensor is streaming leaves the ISP
write masters held and the next start overflows into the watchdog
(the phone hangs).  With the sensor paused the teardown is safe.
`scripts/deploy-firmware.sh` waits for the last `[stream]` line in
`camd.log` to say `sensor off` before it restarts, and refuses if a
camera page is holding the stream open.  The same rule applies to any
manual `setprop ctl.stop krux`.

## The whole thing, in one go

```sh
set -e
P=/home/user/Develop/src/krux-nexus
cd $P

# the kernel (host arm-linux-gnueabihf, the default)
bash tools/build-kernel.sh --tree linux --out /tmp/opencode/ko

# the firmware and the camera daemon
bash scripts/build/build-micropython.sh
bash scripts/cam/build-camd.sh

# the stub, the development ramdisk and the boot image
bash rootfs/build-stub-init.sh
bash rootfs/build-stub-ramdisk.sh
bash rootfs/make-boot-image.sh \
    artifacts/twrp-3.7.0_9-HH.R.17-MADV_WIPEONFORK.img \
    /tmp/opencode/ko/arch/arm/boot/zImage-dtb \
    artifacts/built/boot-stub.img

# the standard image (read-only firmware, TWRP by default)
bash rootfs/build-stub-ramdisk.sh --standard --bundle --force
RAMDISK=artifacts/built/ramdisk-bundled \
    bash rootfs/make-boot-image.sh \
    artifacts/twrp-3.7.0_9-HH.R.17-MADV_WIPEONFORK.img \
    /tmp/opencode/ko/arch/arm/boot/zImage-dtb \
    artifacts/built/boot-standard.img

# deploy and boot (development image)
bash scripts/deploy-firmware.sh
adb reboot bootloader
fastboot boot artifacts/built/boot-stub.img
```

## The tests

```sh
scripts/misc/run-krux-tests.sh
```

One upstream test -- the datum tool's "scan a QR" abort test --
allocates without bound, so the runner caps the address space and
leaves that test out; without the cap a test run can take the machine
down.  The suite takes under two minutes and finishes with the same
failures as before (they are all missing modules: `uUR` and embit's
extras).

The documentation's links can be checked with:

```sh
python3 scripts/misc/check-doc-links.py docs
```

## The one rule: the project tree is the source, `/data/krux` is a copy

There are two places the firmware can live on the device:

| | what it is | when it is used |
|---|---|---|
| `/data/krux` | the development copy, pushed over adb by `scripts/deploy-firmware.sh` | in the development image, **always** |
| `/opt/krux` | a read-only copy baked into the boot image by `build-stub-ramdisk.sh --bundle` | in the standard image, **always** |

Which one is used follows the image, not what is on the data
partition: the development image runs `/data/krux` (so a push always
takes effect), the standard image runs `/opt/krux` (so the data
partition holds the settings only).  During development the image is
built **without** `--bundle`, so there is exactly one copy of the
firmware on the device and no way to be unsure which one is running.

The rule that follows from this:

* **Edit the project tree** -- `krux/src/...` for the firmware,
  `cam/camd.c` for the camera daemon, `krux/firmware/micropython` for
  the interpreter.  Nothing else.
* **Deploy, do not edit** -- `scripts/deploy-firmware.sh` pushes the
  tree to `/data/krux`.  The files on the device are a deployment
  artifact.  Never hand-edit them, and never treat what is on a device
  as a newer version of the source: a device that has not been
  re-deployed holds an *older* tree, not a newer one.

## What is NOT in this repository, and why

* **the kernel** (`linux/`) and **Krux firmware** (`krux/`) are top-level
  git submodules. MicroPython and cUR are nested submodules of the Krux fork
  (`krux/firmware/micropython` and its `lib/cUR`). They are large and have
  their own history; the port's changes are committed *inside* them (branch
  `nexus-port` in `krux`, `master` in MicroPython).
* **the AOSP prebuilt** softfloat toolchain (136 MB) is not tracked; it
  is fetched by the build scripts when needed.
* **the stock boot images** are in `artifacts/` (gitignored); the
  ramdisk is taken out of the ROM's boot image by
  `rootfs/build-stub-ramdisk.sh`.
* **the captures of the bring-up** are in `artifacts/cam-2026-11-11/`
  (gitignored): the vendor's request, the boot images that were used,
  the framebuffer dumps and the one-off tools.  They are the record of
  how the camera was brought up; see [hw-bringup.md](hw-bringup.md).

## The cache

The build trees are large (the kernel's is about 700 MB, the
firmware's 170 MB) and disposable.  **They live in `/tmp`**, which is
a tmpfs here (7.8 GB), and building on it is much faster than
building on the disk -- so the kernel's build tree is one of the
things the space in `/tmp` is for:

```
/tmp/opencode/ko-arch2        the kernel's build tree
/tmp/opencode/mp-build        the firmware's build tree
```

Keep an eye on the space:

```sh
df -h /tmp
```

The toolchain is not a `/tmp` concern: every component is built with
the host's `arm-linux-gnueabihf-gcc`, which is already installed.

(The MicroPython source now lives in the Krux fork; its `build-standard`
directory is disposable and can be removed or rebuilt at any time.)
