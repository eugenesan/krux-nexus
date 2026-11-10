# BUILD-GUIDE.md -- how to build the firmware from scratch

This is the shortest path from a fresh checkout to a running Krux
firmware on the hammerhead (the LG Nexus 5).

Everything here is in this repository; the only things that are not
are the two upstream trees (the kernel and the firmware's own source)
and the toolchain, and those are fetched or unpacked by the scripts
below.

## What has to be built

| what | how | where it ends up |
|---|---|---|
| the **init stub** | `rootfs/build-stub-init.sh` | `rootfs/stub/krux-init` |
| the **stub ramdisk** | `rootfs/build-stub-ramdisk.sh` | `artifacts/built/ramdisk-stub` |
| the **boot image** | `rootfs/make-boot-image.sh` | `artifacts/built/boot-stub.img` |
| the **firmware** (the MicroPython interpreter) | `scripts/build/build-micropython.sh` | `rootfs/micropython-arm32` |
| the **kernel** | `tools/build-kernel.sh` | the output directory you name |
| the **deploy** | `scripts/deploy-firmware.sh` | on the device, in `/data/krux` |

Build outputs under `artifacts/built/` are **gitignored**; do not check
them in, and do not write ramdisks or images under `rootfs/`.

The boot image is only needed when the **kernel** changes; during
development the firmware is deployed over adb and does not need one. For a
finished device, use the bundled image below so the firmware is read-only.

## 0. The tools

* `arm-linux-gnueabihf-gcc` (the host's cross compiler) -- for the
  firmware and the stub.
* `python3`, `cpio`, `gzip`, `zstd`, `git`, `make`, `bc`, `flex`,
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
bash rootfs/build-stub-init.sh
bash rootfs/build-stub-ramdisk.sh
```

The stub is the program that stands in for TWRP's own `init`: it
waits for adb, waits for the volume-up key (that is how the user asks
for TWRP), sets the backlight to half, and then hands the display
and the touch screen to the firmware.

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

## 5. Deploy

```sh
bash scripts/deploy-firmware.sh
```

That pushes the interpreter, the Krux package and the port's own
modules to the device, clears the byte-code cache and restarts the
firmware (`setprop ctl.stop krux` / `setprop ctl.start krux`).  The
firmware's log is `/data/krux/fw.log`.

## 5b. A read-only, bundled boot image

For a finished device, put the firmware in the boot image instead of
`/data/krux`. `--bundle` copies the interpreter, Krux and embit into
`/opt/krux` inside the ramdisk:

```sh
bash scripts/build/build-micropython.sh
bash rootfs/build-stub-ramdisk.sh --bundle
RAMDISK=artifacts/built/ramdisk-bundled \
    bash rootfs/make-boot-image.sh \
    artifacts/twrp-3.7.0_9-HH.R.17-MADV_WIPEONFORK.img \
    /tmp/opencode/ko/arch/arm/boot/zImage-dtb \
    artifacts/built/boot-bundled.img
```

The init stub prefers `/data/krux` and falls back to `/opt/krux`, so the
adb deploy path above is the one that takes effect during development: a
push to `/data/krux` is always the code that runs, and the bundled copy
is only used on a device that has no development tree of its own.
Settings and the log still live on `/data/krux`; the firmware and its
Python libraries are read-only in the boot image.

The ramdisk build prunes what the stub does not use: TWRP's
`terminfo` and Python 3.10 trees, the UBSan runtime (no binary has a
`DT_NEEDED` on it), and the keystore helpers.  `KEEP_KEYSTORE=1` keeps
the keystore binaries for TWRP FBE.  The CPP firmware is kept only in
`/lib/firmware`; `/opt/krux` no longer carries a duplicate.  TWRP's `adbd`, linker, libc and libdexfile are stripped (the dynamic
symbols stay).  The init stub and camd are built with `-Os -s` (camd also uses
`-ffunction-sections -fdata-sections -Wl,--gc-sections`).

The ramdisk is compressed with **XZ** by default (`COMPRESS=xz`); the
kernel must have `CONFIG_RD_XZ=y` (it is set in
`lineageos_hammerhead_defconfig`).  XZ takes the 30.7 MB cpio from
14.73 MB (gzip -9) down to **8.86 MB**.  With the pruned tree the
bundled camera image is about **15.95 MB**, leaving several MB under
hammerhead's boot-partition limit (`0x1600000` bytes).
`make-boot-image.sh` still fails rather than emitting an image that
cannot be flashed.  Use `COMPRESS=gzip` for a kernel without
`CONFIG_RD_XZ`; `lzma` and `lz4` are also accepted, but LZ4 is larger
than gzip here.

`kexec` is **opt-in** because only the memory-wipe work needs it:

```sh
KEXEC=artifacts/built/kexec-arm bash rootfs/build-stub-ramdisk.sh --bundle
```

Without `KEXEC`, neither `/sbin/kexec` nor `/sbin/krux-wipe` is
installed; the firmware falls back to the normal powerctl shutdown.

## 6. Boot it

The images are **not flashed** -- they are booted, so the phone can
always be brought back with its own boot partition:

```sh
adb reboot bootloader
fastboot boot artifacts/built/boot-stub.img
```

(If the device is in the ROM's Android, `adb reboot bootloader`
works; from fastboot, `fastboot boot` works.)

## The whole thing, in one go

```sh
set -e
P=/home/user/Develop/src/krux-nexus
cd $P

# the kernel (host arm-linux-gnueabihf, the default)
bash tools/build-kernel.sh --tree linux --out /tmp/opencode/ko

# the firmware
bash scripts/build/build-micropython.sh

# the stub, the development ramdisk and the boot image
bash rootfs/build-stub-init.sh
bash rootfs/build-stub-ramdisk.sh
bash rootfs/make-boot-image.sh \
    artifacts/twrp-3.7.0_9-HH.R.17-MADV_WIPEONFORK.img \
    /tmp/opencode/ko/arch/arm/boot/zImage-dtb \
    artifacts/built/boot-stub.img

# optional: the read-only bundled image
bash rootfs/build-stub-ramdisk.sh --bundle
RAMDISK=artifacts/built/ramdisk-bundled \
    bash rootfs/make-boot-image.sh \
    artifacts/twrp-3.7.0_9-HH.R.17-MADV_WIPEONFORK.img \
    /tmp/opencode/ko/arch/arm/boot/zImage-dtb \
    artifacts/built/boot-bundled.img

# deploy and boot (development image)
bash scripts/deploy-firmware.sh
adb reboot bootloader
fastboot boot artifacts/built/boot-stub.img
```

## The one rule: the project tree is the source, `/data/krux` is a copy

There are two places the firmware can live on the device:

| | what it is | when it is used |
|---|---|---|
| `/data/krux` | the development copy, pushed over adb by `scripts/deploy-firmware.sh` | **always, during development** |
| `/opt/krux` | a read-only copy baked into the boot image by `build-stub-ramdisk.sh --bundle` | only on a device with no `/data/krux` tree of its own |

`rootfs/init-stub.c` picks `/data/krux` first, so a push always takes
effect.  The bundled copy is a fallback for a finished, production
device; until the project is finalised the development image is built
**without** `--bundle`, so there is exactly one copy of the firmware on
the device and no way to be unsure which one is running.

The rule that follows from this:

* **Edit the project tree** -- `krux/src/...` for the firmware,
  `cam/camd.c` for the camera daemon, `krux/firmware/micropython` for
  the interpreter.  Nothing else.
* **Deploy, do not edit** -- `scripts/deploy-firmware.sh` pushes the
  tree to `/data/krux`.  The files on the device are a deployment
  artifact.  Never hand-edit them, and never treat what is on a device
  as a newer version of the source: a device that has not been
  re-deployed holds an *older* tree, not a newer one.

`deploy-firmware.sh` also removes the `.py` files on the device that the
project tree does not have (a stray file would shadow the tree, because
the firmware imports by name) and rebuilds `camd` when `cam/camd.c` is
newer than the binary, so the daemon can never drift from its source.

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
