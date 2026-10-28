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
| the **stub ramdisk** | `rootfs/build-stub-ramdisk.sh` | `rootfs/stock/ramdisk-stub` |
| the **boot image** | `rootfs/make-boot-image.sh` | `rootfs/stock/boot-stub.img` |
| the **firmware** (the MicroPython interpreter) | `scripts/build/build-micropython.sh` | `kruxport/micropython-arm32` |
| the **kernel** | `tools/build-kernel.sh` | the output directory you name |
| the **deploy** | `scripts/deploy-firmware.sh` | on the device, in `/data/krux` |

The boot image is only needed when the **kernel** changes; the
firmware is deployed over adb and does not need one.

## 0. The tools

* `arm-linux-gnueabihf-gcc` (the host's cross compiler) -- for the
  firmware and the stub.
* `python3`, `cpio`, `gzip`, `zstd`, `git`, `make`, `bc`, `flex`,
  `bison` -- for the builds.
* `adb` and `fastboot` -- to talk to the device.
* `dtc` -- to look at the device trees (optional).

**The toolchain the kernel is built with** is Arch's
`arm-none-eabi-gcc` 16.2.0, the one the ROM's own kernels were built
with.  It is about 2 GB unpacked, so it is not tracked; fetch it with:

```sh
sh tools/fetch-toolchain.sh /tmp/opencode/tc-arch
```

and point the kernel build at it with
`TOOLCHAIN=arch TOOLCHAIN_PATH=/tmp/opencode/tc-arch`.  (On this
machine it already lives at
`/home/user/Develop/toolchains/arch-16.2.0`.)

**The armel sysroot** (the headers and the static libc the
firmware is built against) is built from the vendored `.deb`
packages:

```sh
sh scripts/build-armel-sysroot.sh /tmp/opencode/armel-sysroot
```

## 1. The kernel

```sh
TOOLCHAIN=arch TOOLCHAIN_PATH=/tmp/opencode/tc-arch \
    bash tools/build-kernel.sh --out /tmp/opencode/ko
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
    rootfs/stock/boot-ourkern-clean-twrp.img \
    /tmp/opencode/ko/arch/arm/boot/zImage-dtb \
    rootfs/stock/boot-stub.img
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

About three minutes.  It builds the `micropython` submodule at HEAD
(unix port, 1.30.0-dev)
with the port's own modules -- the display, the input devices, the
NFC, the I2C bus and the rest of the `machine` module -- and leaves
`kruxport/micropython-arm32`.

The build tree is disposable and goes to `BUILD=` (by default
`/tmp/opencode/mp-build`); the source tree is the `micropython`
submodule, which is a git checkout and is
**not** copied around: the build makes a worktree of it at the tag it
builds.

## 5. Deploy

```sh
bash scripts/deploy-firmware.sh
```

That pushes the interpreter, the Krux package and the port's own
modules to the device, clears the byte-code cache and restarts the
firmware (`setprop ctl.stop krux` / `setprop ctl.start krux`).  The
firmware's log is `/data/krux/fw.log`.

## 6. Boot it

The images are **not flashed** -- they are booted, so the phone can
always be brought back with its own boot partition:

```sh
adb reboot bootloader
fastboot boot rootfs/stock/boot-stub.img
```

(If the device is in the ROM's Android, `adb reboot bootloader`
works; from fastboot, `fastboot boot` works.)

## The whole thing, in one go

```sh
set -e
P=/home/user/Develop/src/krux-nexus
cd $P

# the toolchain
mkdir -p /tmp/opencode/tc-arch
sh tools/fetch-toolchain.sh /tmp/opencode/tc-arch

# the sysroot
[ -d /tmp/opencode/armel-sysroot ] || \
    sh scripts/build-armel-sysroot.sh /tmp/opencode/armel-sysroot

# the kernel
TOOLCHAIN=arch TOOLCHAIN_PATH=/tmp/opencode/tc-arch \
    bash tools/build-kernel.sh --out /tmp/opencode/ko

# the stub, the ramdisk and the boot image
bash rootfs/build-stub-init.sh
bash rootfs/build-stub-ramdisk.sh
bash rootfs/make-boot-image.sh \
    rootfs/stock/boot-ourkern-clean-twrp.img \
    /tmp/opencode/ko/arch/arm/boot/zImage-dtb \
    rootfs/stock/boot-stub.img

# the firmware
bash scripts/build/build-micropython.sh

# deploy and boot
bash scripts/deploy-firmware.sh
adb reboot bootloader
fastboot boot rootfs/stock/boot-stub.img
```

## What is NOT in this repository, and why

* **the kernel** (`linux/`), **the firmware** (`krux/`) and
  **MicroPython** (`micropython/`) are git submodules.  They are large
  and they have their own history; the port's own changes are committed
  *inside* them (branch `nexus-port` in `krux`, `master` in
  `micropython`).
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
/tmp/opencode/armel-sysroot   the sysroot the firmware is built against
```

Keep an eye on the space:

```sh
df -h /tmp
```

The **toolchain** is the exception: the unpacked Arch toolchain is
2 GB, which is more than a tenth of `/tmp`, so it lives on the disk
at `/home/user/Develop/toolchains/arch-16.2.0` and the build is
pointed at it with `TOOLCHAIN_PATH`.

(The one thing that must *not* be moved is the micropython build
tree: it is a git **worktree**, and its links point into
`micropython/.git/worktrees/...`, so moving it
breaks the submodule check.  If it is moved, remove it and let the
build make it again.)
