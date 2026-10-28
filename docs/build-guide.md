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
| the **init stub** | `standalone/rootfs/build-stub-init.sh` | `standalone/rootfs/stub/krux-init` |
| the **stub ramdisk** | `standalone/rootfs/build-stub-ramdisk.sh` | `standalone/rootfs/stock/ramdisk-stub` |
| the **boot image** | `standalone/rootfs/make-boot-image.sh` | `standalone/rootfs/stock/boot-stub.img` |
| the **firmware** (the MicroPython interpreter) | `standalone/kruxport/build-micropython.sh` | `standalone/kruxport/micropython-arm32` |
| the **kernel** | `build/build-kernel.sh` | the output directory you name |
| the **deploy** | `standalone/scripts/deploy-firmware.sh` | on the device, in `/data/krux` |

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
`arm-none-eabi-gcc` 16.2.0, the one the ROM's own kernels were
built with.  It is in the repository as two compressed packages:

```
standalone/vendor/toolchain/arm-none-eabi-gcc-16.2.0-1-x86_64.pkg.tar.zst
standalone/vendor/toolchain/arm-none-eabi-binutils-2.47-1-x86_64.pkg.tar.zst
```

Unpack them (about eleven seconds, 2 GB) wherever you like -- the
build does not care where they are:

```sh
mkdir -p /tmp/opencode/tc-arch
cd /tmp/opencode/tc-arch
for p in /path/to/the/project/standalone/vendor/toolchain/*.pkg.tar.zst; do
    tar --use-compress-program=zstd -xf "$p"
done
TOOLCHAIN_PATH=/tmp/opencode/tc-arch
```

or let the script do it (it downloads what is missing and unpacks
what is there):

```sh
sh standalone/tools/fetch-toolchain.sh
```

**The armel sysroot** (the headers and the static libc the
firmware is built against) is built from the vendored `.deb`
packages:

```sh
sh standalone/tools/build-armel-sysroot.sh /tmp/opencode/armel-sysroot
```

## 1. The kernel

```sh
TOOLCHAIN=arch TOOLCHAIN_PATH=/tmp/opencode/tc-arch \
    bash build/build-kernel.sh --out /tmp/opencode/ko
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
(`standalone/tools/profiles/hammerhead.list`), which switches the
radio off, and verifies afterwards that nothing turned it back on.

## 2. The init stub and the ramdisk

```sh
bash standalone/rootfs/build-stub-init.sh
bash standalone/rootfs/build-stub-ramdisk.sh
```

The stub is the program that stands in for TWRP's own `init`: it
waits for adb, waits for the volume-up key (that is how the user asks
for TWRP), sets the backlight to half, and then hands the display
and the touch screen to the firmware.

## 3. The boot image

```sh
bash standalone/rootfs/make-boot-image.sh \
    standalone/rootfs/stock/boot-ourkern-clean-twrp.img \
    /tmp/opencode/ko/arch/arm/boot/zImage-dtb \
    standalone/rootfs/stock/boot-stub.img
```

The template is a boot image that is known to boot on this device;
the script takes its header (the load addresses, the page size, the
command line) and swaps in our kernel and our ramdisk.  **Do not
invent those numbers**: the stock bootloader loads the kernel and the
ramdisk at exactly the addresses in the header, and getting them
wrong produces a device that boots to nothing at all.

## 4. The firmware

```sh
bash standalone/kruxport/build-micropython.sh
```

About three minutes.  It builds MicroPython 1.28.0 (the unix port)
with the port's own modules -- the display, the input devices, the
NFC, the I2C bus and the rest of the `machine` module -- and leaves
`standalone/kruxport/micropython-arm32`.

The build tree is disposable and goes to `BUILD=` (by default
`/tmp/opencode/mp-build`); the source tree is
`standalone/vendor/micropython`, which is a git checkout and is
**not** copied around: the build makes a worktree of it at the tag it
builds.

## 5. Deploy

```sh
bash standalone/scripts/deploy-firmware.sh
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
fastboot boot standalone/rootfs/stock/boot-stub.img
```

(If the device is in the ROM's Android, `adb reboot bootloader`
works; from fastboot, `fastboot boot` works.)

## The whole thing, in one go

```sh
set -e
P=/home/user/Develop/src/android-air-gapped
cd $P

# the toolchain
mkdir -p /tmp/opencode/tc-arch
(cd /tmp/opencode/tc-arch && for p in $P/standalone/vendor/toolchain/*.pkg.tar.zst; do
    tar --use-compress-program=zstd -xf "$p"; done)

# the sysroot
[ -d /tmp/opencode/armel-sysroot ] || \
    sh standalone/tools/build-armel-sysroot.sh /tmp/opencode/armel-sysroot

# the kernel
TOOLCHAIN=arch TOOLCHAIN_PATH=/tmp/opencode/tc-arch \
    bash build/build-kernel.sh --out /tmp/opencode/ko

# the stub, the ramdisk and the boot image
bash standalone/rootfs/build-stub-init.sh
bash standalone/rootfs/build-stub-ramdisk.sh
bash standalone/rootfs/make-boot-image.sh \
    standalone/rootfs/stock/boot-ourkern-clean-twrp.img \
    /tmp/opencode/ko/arch/arm/boot/zImage-dtb \
    standalone/rootfs/stock/boot-stub.img

# the firmware
bash standalone/kruxport/build-micropython.sh

# deploy and boot
bash standalone/scripts/deploy-firmware.sh
adb reboot bootloader
fastboot boot standalone/rootfs/stock/boot-stub.img
```

## What is NOT in this repository, and why

* **the kernel tree** (`standalone/vendor/android_kernel_lge_hammerhead`)
  and **the firmware's source** (`standalone/vendor/krux`,
  `standalone/vendor/micropython`) are git checkouts.  They are large
  and they have their own history, so they are kept on disk and not
  tracked; the build scripts work with the tree as it is, and the
  port's own changes are *on top* of them (the port's files, the
  kernel's device tree, the profile).
* **the AOSP prebuilt** (`standalone/vendor/toolchain/gcc-androideabi-4.9`,
  136 MB) is the softfloat toolchain the init stub and the firmware
  are built with.
* **the ROM's Android ramdisk** is in
  `standalone/rootfs/stock/boot.img` (the ROM's own boot image); the
  ramdisk is taken out of it by `build-stub-ramdisk.sh`.

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
`standalone/vendor/micropython/.git/worktrees/...`, so moving it
breaks the submodule check.  If it is moved, remove it and let the
build make it again.)
