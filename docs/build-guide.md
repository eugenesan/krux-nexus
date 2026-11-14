# BUILD-GUIDE.md -- how to build the firmware from scratch

This is the shortest path from a fresh checkout to a running Krux
firmware on the hammerhead (the LG Nexus 5).

Everything here is in this repository; the two upstream trees (the
kernel and the firmware's own source) are submodules, and the toolchain
is the host's `arm-linux-gnueabihf-gcc`.  Nothing has to be downloaded
for the kernel or the firmware.  Start with:

```sh
git submodule update --init --recursive
```

The **delivery modes** are the thing to understand first, because they
decide which image you build and what runs when the device comes up.

| mode | the firmware lives | what starts at boot | how the firmware is started |
|---|---|---|---|
| **development** (bench) | `/data/krux`, pushed over adb | the firmware, automatically | -- |
| **standard** | `/opt/krux`, read-only in the recovery image | **nothing**: the device comes up in TWRP | the volume-up key during the boot, or `/sbin/start-krux` |
| **extreme** | a minimal initramfs, no partitions | the firmware | -- (deferred: the UART exists, the minimal ramdisk does not) |

The plan for all three, and what was measured, is in
[packaging-plan.md](packaging-plan.md).

The delivered build is the **standard** mode in its **sealed** variant
(`KRUX_RELEASE=1`, `KRUX_SEAL=1`, the default).  Release is the default
for every low-level builder; development and the audit image are explicit
opt-outs.  A standard image written to the recovery partition comes up in
TWRP with adb off; the firmware runs from the read-only `/opt/krux` copy.

## What has to be built

| what | how | where it ends up |
|---|---|---|
| the **init stub** | `rootfs/build-stub-init.sh` | `rootfs/stub/krux-init` |
| the **stub ramdisk** | `rootfs/build-stub-ramdisk.sh` | `artifacts/built/ramdisk-stub` (or `-bundled`) |
| the **boot image** | `rootfs/make-boot-image.sh`, or `scripts/build/build-release.sh` | `artifacts/built/boot-standard-release.img` |
| the **firmware** (the MicroPython interpreter) | `scripts/build/build-micropython.sh` | `rootfs/micropython-arm32` (release: `-release`) |
| the **camera daemon** | `scripts/cam/build-camd.sh` | `cam/camd` (release: `camd-release`) |
| the **kernel** | `tools/build-kernel.sh` | `artifacts/built/ko` by default in the wrappers |
| the **deploy** (development only) | `scripts/deploy-firmware.sh` | on the device, in `/data/krux` |

Build outputs under `artifacts/` are **gitignored**; do not check them
in, and do not write ramdisks or images under `rootfs/`.  `artifacts/`
is on disk; `/tmp` is a tmpfs and must not hold anything that has to
survive a reboot.

The boot image is only needed when the **kernel** or the ramdisk
changes; during development the firmware is deployed over adb and does
not need one.

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
bash tools/build-kernel.sh --tree linux --out artifacts/built/ko
```

That takes about fifteen minutes on four cores (the tree is a CAF
3.4 tree with about 2,500 translation units) and leaves:

```
artifacts/built/ko/arch/arm/boot/zImage-dtb
artifacts/built/ko/.config
artifacts/built/ko.build.log
```

**Use `zImage-dtb`, not `zImage`.**  The bootloader finds the device
tree by scanning the kernel image for it, and a plain compressed
`zImage` has none: the bootloader answers "dtb not found" and
refuses to boot.  (`zImage-dtb` is the compressed kernel with the
device trees appended.)

The build uses `lineageos_hammerhead_defconfig`, applies the air-gap
profile (`tools/profiles/hammerhead.list`), which switches the radio
off, and verifies afterwards that nothing turned it back on.  The
defconfig carries the firmware's kernel features:

- `CONFIG_KERNEL_XZ=y` — XZ-compressed kernel (~4.1 MB);
- `CONFIG_RD_XZ=y` — XZ ramdisk support for the `fastboot boot` path;
- `CONFIG_MEMORY_WIPE=y` — the `wipe=` engine;
- `CONFIG_MEMORY_WIPE_ON_POWEROFF=y` — the power-off marker.

The release hardening fragment `scripts/build/krux-release.config` is a
manual step: `.config` + `olddefconfig` + rebuild.  It turns off the ram
console, ramoops/pstore and core files, but **keeps
`CONFIG_ANDROID_PERSISTENT_RAM=y`**, because the wipe marker lives in the
reserved 1 MB region at `0x100F0000`.  The current shipping kernel still
has the three channels on; see `security-audit.md`, Phase 3.

## 2. The init stub and the ramdisk

```sh
bash rootfs/build-stub-init.sh                 # the development stub
bash rootfs/build-stub-ramdisk.sh
```

The stub is the program that runs as init's `krux` service: it waits a
short USB window, waits for the volume-up key, sets the backlight to
half, and then either starts TWRP or hands the display and the touch
screen to the firmware.

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

* **development** (bench): no key -- the firmware starts; the
  volume-up key -- TWRP.  The firmware is the copy on `/data/krux`, so
  a push over adb takes effect.
* **standard** (`--standard`): no key -- TWRP; the volume-up key --
  the firmware, running the read-only copy in `/opt/krux`.  Nothing
  starts by itself, and the data partition holds the settings only.

The mode can be overridden at run time by a `1` in
`/data/krux/state/autostart` **only in a development build**
(`KRUX_RELEASE=0`); the release deliberately does not read the data
partition for this (see *the knobs* below).

The ramdisk build also installs the runtime entry points:

| path | purpose |
|---|---|
| `/sbin/krux-init` | the init stub |
| `/sbin/recovery.stock` | TWRP's binary |
| `/sbin/recovery` | symlink to `recovery.stock` (TWRP's sideload child needs it) |
| `/sbin/start-krux` | stop TWRP and start the firmware now |
| `/sbin/start-adb` | restore `adbd` and bring adb up on demand |
| `/sbin/start-serial` | root shell on the UART (`ttyHSL0`, 115200 8N1) |
| `/system/bin/sh` | symlink to `/sbin/sh`, for TWRP's `system()` |

`KRUX_SEAL=1` (the default) hides `/sbin/adbd` as `/sbin/adbd.off` in
every stub path, so TWRP's init cannot start it; `start-adb` restores
it.  `KRUX_RELEASE=1` (the default) also compiles the logs, the frame
dumps and `save_ppm` out of the firmware and makes the stub ignore the
`autostart` file.

The ramdisk build prunes what the stub does not use: TWRP's
`terminfo` and Python 3.10 trees, the UBSan runtime (no binary has a
`DT_NEEDED` on it), and the keystore helpers.  The CPP firmware is kept
only in `/lib/firmware`; `/opt/krux` no longer carries a duplicate.
TWRP's `adbd`, linker, libc and libdexfile are stripped (the dynamic
symbols stay).  The init stub and camd are built with `-Os -s` (camd
also uses `-ffunction-sections -fdata-sections -Wl,--gc-sections`).

**Compression.**  The default is **gzip**, and the delivered image must
keep it: it is *flashed* to the recovery partition, and the stock
bootloader only unpacks a gzip ramdisk -- an XZ ramdisk there boots
nothing, silently.  On the `fastboot boot` development path the *kernel*
unpacks the ramdisk, and the kernel has `CONFIG_RD_XZ=y`
(`lineageos_hammerhead_defconfig`), so `COMPRESS=xz` works there and
makes the ramdisk 8.7 MB against 14.5 MB.  Use XZ only for images you
boot, never for an image you flash.

## 3. The boot image

```sh
bash rootfs/make-boot-image.sh \
    artifacts/twrp-3.7.0_9-HH.R.17-MADV_WIPEONFORK.img \
    artifacts/built/ko/arch/arm/boot/zImage-dtb \
    artifacts/built/boot-standard-release.img
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

## 5. Deploy (development image only)

```sh
bash scripts/deploy-firmware.sh
```

That pushes the interpreter, the Krux package, the port's own modules,
embit and the camera daemon to the device, clears the byte-code cache
and restarts the firmware (`setprop ctl.stop krux` /
`setprop ctl.start krux`).  It needs the device in the **development**
image with adb reachable; the standard sealed image has no adb until
`/sbin/start-adb` is run, and runs `/opt/krux` instead of
`/data/krux`, so a push would not be used there.  The firmware's log is
`/data/krux/fw.log`, the daemon's is `/data/krux/camd.log`.

It also removes the `.py` files on the device that the project tree
does not have (a stray file would shadow the tree, because the
firmware imports by name) and rebuilds `camd` when `cam/camd.c` is
newer than the binary, so the daemon can never drift from its source.

## 5b. The standard image: read-only firmware, nothing starts by itself

The delivered image is built by `scripts/build/build-release.sh` (see
§5c); it is the standard, sealed image.  The manual steps it wraps are:

```sh
OUT=rootfs/micropython-arm32-release bash scripts/build/build-micropython.sh
KRUX_RELEASE=1 bash scripts/cam/build-camd.sh cam/camd-release
MICROPYTHON="$PWD/rootfs/micropython-arm32-release" \
CAMD="$PWD/cam/camd-release" \
    bash rootfs/build-stub-ramdisk.sh --standard --bundle --force
RAMDISK=artifacts/built/ramdisk-bundled \
    bash rootfs/make-boot-image.sh \
    artifacts/twrp-3.7.0_9-HH.R.17-MADV_WIPEONFORK.img \
    artifacts/built/ko/arch/arm/boot/zImage-dtb \
    artifacts/built/boot-standard-release.img
```

The standard image boots into **TWRP 3.7.0** (the menu draws, `/data`
is mounted), and the firmware waits to be asked:

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

The sealed image has adb **off**.  For diagnostics, from TWRP's terminal:

```sh
/sbin/start-adb      # restore adbd and enable adb
/sbin/start-serial   # root shell on the UART (host /dev/ttyUSB0)
```

For scripted UART checks from the host:
`python3 scripts/misc/serial-cmd.py "id" "ps -A"` (it uses termios, no
extra Python packages).

Sideload does not need `start-adb`: use Advanced → ADB Sideload from a
fresh boot, with adbd off.  If adbd is running, stop it first
(`setprop ctl.stop adbd`) so `minadbd` can own the USB endpoint.

Two behaviours of the standard mode are worth knowing before an image
is given to somebody else:

* **"Erase User's Data" empties the data partition.**  It removes the
  settings and the encrypted mnemonics, so the device comes up with the
  same firmware and no state.  That is only safe because the firmware is
  in the image, not on `/data`.
* **the device powers itself off when it is left alone.**  The idle
  timeout calls the firmware's own shutdown -- in the bundled mode
  only; a development device stays alive and reachable over adb.

## 5c. Development and release builds

The **release profile is the default** at the build layer
(`KRUX_RELEASE=1`, `KRUX_SEAL=1`); development and the final audit are
explicit opt-outs, so a forgotten flag cannot produce a debug image.

```sh
# development: logs, frame dumps, adb, save_ppm
bash scripts/build/build-dev.sh            # -> artifacts/built/boot-dev.img

# release, sealed (no adb): the shipping image
bash scripts/build/build-release.sh        # -> artifacts/built/boot-standard-release.img

# release with adb kept, for the final on-device audit
# -> artifacts/built/boot-standard-release-audit.img
KRUX_SEAL=0 bash scripts/build/build-release.sh
```

Both wrappers use `${KERNEL_OUT:-artifacts/built/ko}` and build the
kernel there on the first run; set `KERNEL_OUT=` to choose another
build.  `KERNEL_OUT` must point at a directory with
`arch/arm/boot/zImage-dtb`.

`scripts/security-check.sh <image>` is the gate: it greps the release
artifacts and the image for the dump/write capabilities and fails the
build if one is still there.  `scripts/misc/check-airgap.py` is the
wireless gate: it checks the kernel `.config` and the ramdisk for
WLAN/Bluetooth/cellular/FM drivers, userspace and firmware, and allows
only `CONFIG_NFC_BCM2079X`.  Both run from `build-release.sh`.  The seal
(adb off) is deliberately the last step — see
[security-audit.md](security-audit.md), Phase 6.

The release kernel fragment (`scripts/build/krux-release.config`) is
**not applied by these wrappers**; the hardened kernel is a manual
`.config` + `olddefconfig` build.  The shipping image still has the ram
console, ramoops and core files enabled, while keeping
`CONFIG_ANDROID_PERSISTENT_RAM=y` for the wipe marker.

The verified delivered file is `artifacts/built/boot-final-sealed.img`
(sha256 `807facd8…`), which is that build's output under its final name.

## 6. Boot it, and flash it

Two different things, for two different purposes.

**Development / audit: boot without writing anything.**  This is the
safe path and leaves every partition untouched:

```sh
adb reboot bootloader
fastboot boot artifacts/built/boot-dev.img
```

**The delivered firmware: write the recovery partition.**  It is
started from the bootloader's **Recovery mode**; the boot partition is
never touched:

```sh
adb reboot bootloader
fastboot flash recovery artifacts/built/boot-standard-release.img
```

**Never flash `boot`.**  The whole point of the recovery slot is that
the phone's normal boot path stays intact and recoverable.

A plain power-on starts the boot partition (Android, or whatever it
holds), not the recovery image.  The wipe path is the exception: it
sets the bootloader's restart reason to `recovery`, so the next boot
enters the recovery image directly.

## The knobs

Two places, and the difference matters:

| | what is in it | when it is read |
|---|---|---|
| `/data/krux/state/` | the **persistent** knobs: `stream`, `mode`, `cycle`, `capture_cfg`, `fov`, `profile`, `autostart` | at every start of the daemon |
| `/tmp/krux/` | the **volatile** ones: `af`, `focus`, `luma_gain`, `unsharp` | at every start; `/tmp` is a tmpfs, so they are gone after a reboot |

| knob | what it does |
|---|---|
| `stream` | 0 = the raw Bayer through the CPP, 1 = the vendor's processed preview (PIX_VIEWFINDER).  1 is the default |
| `mode` | 0 = the binned 1640x1232 sensor mode, 1 = the full 3280x2464 one.  The processed preview forces the full mode |
| `cycle` | the vendor's per-frame ISP cycle (on by default) |
| `capture_cfg` | the vendor's per-capture ISP configuration (off by default) |
| `fov` | the window: 0 = a 2160 square in the centre, 1 = the whole frame |
| `autostart` | 1 = start the firmware at boot even in the standard image (development builds only; the release ignores it) |

**The stream runs continuously.**  An on-demand pause at the sensor
was tried on 2026-11-12 and reverted: after a long pause the ISP/CPP
came back out of frame sync (random horizontal overlap and an
auto-exposure that pumped).  The autofocus's brief pause is safe; a
long one is not.  See [cam-state.md](cam-state.md).

Restarting the service while the camera is streaming is the fragile
teardown (it can leave the ISP write masters held and hang the next
start); restart when the device is at the menu, not on a camera page.
`scripts/deploy-firmware.sh` still looks for a paused stream in
`camd.log` and waits if the running build reports one, but the current
build does not pause.

## The whole thing, in one go

The delivered firmware, from a fresh checkout:

```sh
set -e
git submodule update --init --recursive

# the kernel: air-gapped, XZ, wipe-capable (~15 min)
bash tools/build-kernel.sh --tree linux --out artifacts/built/ko

# the release firmware, camd, the standard sealed ramdisk and the image
bash scripts/build/build-release.sh
# -> artifacts/built/boot-standard-release.img

# write it to recovery (never to boot)
adb reboot bootloader
fastboot flash recovery artifacts/built/boot-standard-release.img
```

For the bench, use the development image instead:

```sh
bash scripts/build/build-dev.sh
adb reboot bootloader
fastboot boot artifacts/built/boot-dev.img
```

The individual steps are sections 1–5; the wrappers are just those
steps with the release defaults and the right kernel path.

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
* **the old AOSP prebuilt / Arch toolchains** are not tracked and are
  no longer used; the host's `arm-linux-gnueabihf-gcc` builds every
  component.  `docs/toolchain.md` keeps their history.
* **the stock boot images** are in `artifacts/` (gitignored); the
  ramdisk is taken out of the ROM's boot image by
  `rootfs/build-stub-ramdisk.sh`.
* **the captures of the bring-up** are in `artifacts/cam-2026-11-11/`
  (gitignored): the vendor's request, the boot images that were used,
  the framebuffer dumps and the one-off tools.  They are the record of
  how the camera was brought up; see [hw-bringup.md](hw-bringup.md).

## The build trees

The kernel's build tree is large (about 700 MB) and is kept on disk at
`artifacts/built/ko`, because `/tmp` is a tmpfs and a reboot would throw
it away.  The wrappers reuse it; a fresh clone builds it on the first
run.  Do not keep a second copy in `/tmp` -- the old `ko-*` directories
were disposable and are gone.

The MicroPython build tree (`/tmp/opencode/mp-build`, about 170 MB) *is*
disposable: it is recreated from the source submodule on each build.
The same is true of the ramdisk work directory
(`/tmp/opencode/stub-ramdisk`).  Keep an eye on the tmpfs:

```sh
df -h /tmp
```

The toolchain is not a `/tmp` concern: every component is built with
the host's `arm-linux-gnueabihf-gcc`, which is already installed.

(The MicroPython source lives in the Krux fork; its `build-standard`
directory is disposable and can be removed or rebuilt at any time.)
