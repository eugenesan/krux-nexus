# Krux firmware on the LG Nexus 5 (hammerhead)

A self-contained Krux signer for the LG Nexus 5 (MSM8974): TWRP-based
recovery, a MicroPython Krux firmware, an air-gapped kernel, and a
kernel-only RAM wipe on power-off.  There is no Android userspace in the
delivered image.

**Status (2026-11-13): the firmware is built, flashed and verified on the
device.**  The delivered image is the **standard, sealed** build: the
phone comes up in TWRP with adb off; the firmware is started with the
volume-up key or `/sbin/start-krux`.  Camera QR/entropy, NFC/SeedKeeper,
settings, display and power-off all work on the device.

Final artifact:

```
artifacts/built/boot-final-sealed.img
size    20,064,256 bytes
sha256  807facd8a5b521daf675b29e910efb441b77e432873f327030c079c540130892
kernel  673242ee6e09e25edc658dfb6ee05e794eb821e5e35e08f2cd647a3588652805
slot    recovery (/dev/block/mmcblk0p20)
```

The build is verified on device serial `0879524b0068abca`.  Always check
`adb devices` / `fastboot devices` before a command that writes a
partition.

`boot-final-sealed.img` is that verified build's file name.  A fresh
build produces `boot-standard-release.img`: the same source and the same
kernel, but the cpio (and the rebuilt interpreter) can differ
byte-for-byte, so the hash will differ.

---

## 1. What runs

The image is written to the **recovery** partition and started from the
bootloader.  It is never written to `boot`, so the phone can always be
returned to its previous state.

| action | result |
|---|---|
| bootloader → **Recovery mode** (or `fastboot boot`) | **TWRP**, adb **off** (the seal) |
| hold **volume-up** while the device boots | **Krux** firmware |
| TWRP terminal → `start-krux` | TWRP stops and **Krux** starts |
| TWRP terminal → `start-adb` | restores `/sbin/adbd` and brings normal adb up |
| TWRP terminal → `start-serial` | root shell on the UART (`ttyHSL0`, 115200 8N1) |
| Advanced → ADB Sideload | `adb sideload <file>.zip` works |
| any power-off (Krux Shutdown, TWRP, `reboot -p`) | RAM is wiped on the next boot, then the device powers off |

Krux keeps its mutable settings on `/data`; the delivered image carries
the firmware read-only in the ramdisk at `/opt/krux`.  The data partition
therefore holds state, never code.

---

## 2. The repository

The top-level tree is the glue; the two large trees are submodules:

```
linux/    eugenesan/android_kernel_lge_hammerhead, branch airgap/cam-xz-wipe
krux/     eugenesan/krux, branch nexus-port
rootfs/   the init stub, the ramdisk builder, the boot-image packer
cam/      camd, the V4L2/ISP/CPP camera daemon
scripts/  build and deployment wrappers
docs/     the design and bring-up record
artifacts/  upstream images and build output (gitignored)
```

`krux/firmware/micropython` is a nested submodule of the Krux tree.  Clone
with submodules:

```sh
git clone --recursive <this-repository>
cd krux-nexus
git submodule update --init --recursive
```

---

## 3. Prerequisites

On the build host:

| tool | used for |
|---|---|
| `arm-linux-gnueabihf-gcc` | kernel, MicroPython, camd and the init stub |
| `make`, `bc`, `flex`, `bison`, `python3`, `cpio`, `gzip`, `xz`, `git` | the builds |
| `adb`, `fastboot` | deployment and diagnosis |
| `dtc` | optional, to inspect device trees |

The kernel is built with the **host** cross compiler (`TOOLCHAIN=host`,
the default).  Nothing is downloaded for the kernel.  A USB UART adapter
on the headset jack is optional but strongly recommended: it is the way
in when adb is off or broken (`start-serial`).

The device must have an unlocked bootloader and a recovery partition at
least as large as the image (about 23 MB on this phone).

---

## 4. Build from scratch

All commands are run from the repository root.  Build output goes to
`artifacts/`, which is on disk and gitignored; `/tmp` is a tmpfs here and
is emptied by a reboot, so it is **not** used for anything that should
survive.

### 4.1 The kernel

```sh
bash tools/build-kernel.sh --tree linux --out artifacts/built/ko
```

This takes about fifteen minutes on four cores and leaves:

```
artifacts/built/ko/arch/arm/boot/zImage-dtb
artifacts/built/ko/.config
artifacts/built/ko.build.log
```

The build uses `lineageos_hammerhead_defconfig`, applies the air-gap
profile in `tools/profiles/hammerhead.list` (radios off, camera and NFC
kept), and verifies afterwards that nothing re-enabled itself.  The
defconfig already turns on the pieces this firmware needs:

- `CONFIG_KERNEL_XZ=y` — the kernel itself is XZ-compressed (about 4.1 MB);
- `CONFIG_RD_XZ=y` — an XZ ramdisk can be booted with `fastboot boot`;
- `CONFIG_MEMORY_WIPE=y` — the `wipe=` engine;
- `CONFIG_MEMORY_WIPE_ON_POWEROFF=y` — the power-off marker that makes
  every shutdown wipe RAM.

**Use `zImage-dtb`, not `zImage`.**  The bootloader finds the device tree
by scanning the kernel image; a plain `zImage` has none and the
bootloader refuses to boot it.  `tools/build-kernel.sh` prints the image
path it produced.

The release hardening fragment `scripts/build/krux-release.config`
(no ram console, no ramoops/pstore, no core files) is **not applied by
the default build**; it is a manual `.config`/`olddefconfig` step.  The
shipping kernel still has those three on.  The wipe marker is unaffected:
it lives in the reserved persistent-RAM tail, so
`CONFIG_ANDROID_PERSISTENT_RAM=y` must stay even when the ram console is
turned off.  See `docs/security-audit.md`, Phase 3.

### 4.2 The firmware and the camera daemon

```sh
bash scripts/build/build-micropython.sh     # -> rootfs/micropython-arm32
bash scripts/cam/build-camd.sh              # -> cam/camd
```

The MicroPython build is about three minutes.  The source is the
`krux/firmware/micropython` submodule; its disposable build tree defaults
to `/tmp/opencode/mp-build` and is recreated as needed.

`KRUX_RELEASE=1` is the default: the release port has no `save_ppm`, the
release camd has no frame dumps or verbose trace, and the stub logs to
`/dev/null`.  A development build sets `KRUX_RELEASE=0` explicitly.

### 4.3 The sealed image (the deliverable)

```sh
bash scripts/build/build-release.sh
```

This is the standard, sealed image.  It:

1. uses the kernel at `${KERNEL_OUT:-artifacts/built/ko}` (building it
   first if it is missing);
2. builds the release MicroPython port and camd;
3. builds the **standard** ramdisk with the firmware bundled read-only in
   `/opt/krux`;
4. packs `artifacts/built/boot-standard-release.img`;
5. runs `scripts/security-check.sh` as the gate.

`KRUX_SEAL=0` builds the audit variant, which is the same release with
adb left available:

```sh
KRUX_SEAL=0 bash scripts/build/build-release.sh
# -> artifacts/built/boot-standard-release-audit.img
```

`KERNEL_OUT=/path/to/a/kernel/build` selects a different kernel without
editing the script.  The ramdisk compression is gzip, because the
bootloader only unpacks gzip for an image written to a partition.  XZ is
an opt-in for the `fastboot boot` path only.

### 4.4 The development image

```sh
bash scripts/build/build-dev.sh
# -> artifacts/built/boot-dev.img
```

The development image starts the firmware by default, keeps adb and the
logs, and runs the copy deployed to `/data/krux` so a push takes effect
immediately.  It is for the bench, not for a finished signer.

### 4.5 Building just the ramdisk or the image

The individual steps are useful when only one layer changed:

```sh
bash rootfs/build-stub-ramdisk.sh --standard --bundle --force
RAMDISK=artifacts/built/ramdisk-bundled \
    bash rootfs/make-boot-image.sh \
    artifacts/twrp-3.7.0_9-HH.R.17-MADV_WIPEONFORK.img \
    artifacts/built/ko/arch/arm/boot/zImage-dtb \
    artifacts/built/boot-standard-release.img
```

`--standard` makes the standard delivery mode, `--bundle` installs the
firmware in `/opt/krux`, and `--force` replaces the existing ramdisk
without the content comparison.  The TWRP image is the template for the
header, load addresses, page size and cmdline; do not invent those.

---

## 5. What is in the image

The ramdisk is TWRP 3.7.0 with two deliberate changes:

- TWRP's own binary is `/sbin/recovery.stock`; init runs the stub
  (`/sbin/krux-init`) as the `krux` service instead;
- `/sbin/recovery` is a symlink back to `recovery.stock`.  TWRP's
  sideload child hardcodes `execl("/sbin/recovery", "--adbd", ...)`;
  without the symlink it exits 255 before `minadbd` runs.

The runtime entry points are installed by `rootfs/build-stub-ramdisk.sh`:

| path | purpose |
|---|---|
| `/sbin/krux-init` | the init stub: chooses TWRP or the firmware |
| `/sbin/recovery.stock` | TWRP itself |
| `/sbin/recovery` | symlink, for TWRP's sideload child |
| `/sbin/start-krux` | stop TWRP and start the firmware now |
| `/sbin/start-adb` | restore `adbd` and enable adb on demand |
| `/sbin/start-serial` | root shell on the UART |
| `/opt/krux` | the bundled read-only firmware and Python libraries |
| `/system/bin/sh` | symlink to `/sbin/sh`, for TWRP's `system()` |

At boot the stub mounts `/data` and waits for the volume-up key.  In the
standard image, no key means TWRP; the key means the firmware.  The
firmware runs from `/opt/krux`; settings go to `/data/krux`.  The release
build keeps no logs there and does not read
`/data/krux/state/autostart` (that is a development knob).

The seal is absolute: in the sealed build the stub renames `/sbin/adbd`
to `/sbin/adbd.off` in **every** path, TWRP's init cannot restart it, and
the `krux-firmware` service is `disabled`.  There is no adb listener
until `start-adb` is run.

**Wireless:** the standard image has exactly one wireless interface,
the NFC reader (`CONFIG_NFC_BCM2079X`).  The kernel has no WLAN/
`CFG80211`/`BCMDHD`, no Bluetooth/RFCOMM/BNEP/HIDP, no MSS baseband/
RMNET/QMI-WWAN and no FM tuner drivers; the ramdisk has no
`wpa_supplicant`/`hostapd`/`wificond` or Broadcom Wi-Fi/Bluetooth
firmware.  `build-release.sh` runs `scripts/misc/check-airgap.py` as a
build gate, and the ramdisk build prunes TWRP's dead Wi-Fi keystore
HAL.

---

## 6. Deploy

Writing the image to the recovery partition is the only partition write
this project performs.  **Never flash `boot`**: the boot partition stays
untouched, and the phone is recoverable by restarting into its existing
system.

```sh
# from Android: adb reboot bootloader
# or from the bootloader: hold power + volume-down
fastboot devices
fastboot flash recovery artifacts/built/boot-standard-release.img
```

To start it without writing anything (a useful smoke test), `fastboot
boot` starts the image immediately, without changing a partition:

```sh
fastboot boot artifacts/built/boot-standard-release.img
```

To run the image that is already flashed, go to the bootloader and choose
**Recovery mode**.  A plain power-on starts the boot partition, not the
recovery image; recovery is selected explicitly (or by the restart reason
the wipe path sets).

### Using the delivered image

- **TWRP:** the menu appears with adb off.
- **Krux:** hold volume-up while the device boots, or run
  `/sbin/start-krux` in the TWRP terminal.
- **adb shell/push:** run `/sbin/start-adb` in the TWRP terminal.
- **UART:** run `/sbin/start-serial`; the host sees `/dev/ttyUSB0` at
  115200 8N1.  For scripted checks:
  `python3 scripts/misc/serial-cmd.py "id" "ps -A"`.
- **Sideload:** Advanced → ADB Sideload → swipe to start, then
  `adb sideload <file>.zip` on the host.  Sideload does **not** need
  `start-adb`; run it from a fresh boot with adbd off.  If adbd is
  already running, stop it first (`setprop ctl.stop adbd`) or reboot.

The security gate (`scripts/security-check.sh`) runs as part of
`build-release.sh`; it greps the release binaries and the image for the
frame dumps, the QR write and `save_ppm`, and fails the build if one is
reachable.

---

## 7. The RAM wipe on power-off

The wipe is **kernel and RAM only**.  There is no NAND write, no BCB, no
userspace helper, no kexec and no second kernel.

1. `sys_reboot(LINUX_REBOOT_CMD_POWER_OFF)` writes a magic/CRC marker
   into the reserved persistent-RAM tail at `0x100F0000`.
2. The kernel restarts with the bootloader's `recovery` reason (IMEM
   `0x77665502`, the same mechanism as `reboot bootloader`).
3. The next boot reaches `mem_init()`, sees the marker, wipes every free
   page, and powers off from `kernel_init()` **before userspace starts**.
4. The marker is cleared only at that power-off.

The marker is RAM only.  If the recovery boot does not happen, it is gone
with the RAM, and so are the secrets.

Verified trace (`artifacts/built/logs/wipe-serial-2026-11-13.log`):

```
wipe: marking RAM and restarting into recovery
[390] reboot_mode restart reason = recovery
...
wipe: poweroff marker found, wiping this boot
wipe: start (mode 1, test 0 MB, verify 0)
wipe: 1488 MB wiped
wipe: powering off
Powering off the SoC
```

With the USB cable connected the charger can power the phone back into
the recovery/Android path; disconnect USB for a true off.  The RAM is
wiped either way.

**Limit:** the wipe covers free pages.  Reserved memory — the
framebuffer/scanout, the ION/camera buffers and other carveouts — is not
free and is not zeroed by this pass.  The display should be blank before
shutdown on a finished device; the framebuffer scrub is still a tracked
item (`docs/security-audit.md`, Phase 4).

---

## 8. Verifying an image

```sh
sha256sum artifacts/built/boot-standard-release.img
# compare with the hash recorded with the build

# the image is a boot image; inspect it without flashing
python3 rootfs/bootimg.py unpack artifacts/built/boot-standard-release.img /tmp/chk
```

The kernel and ramdisk can be checked independently; the final delivered
image hashes recorded above are the reference.  The build is not
byte-reproducible: the cpio carries inode numbers and mtimes, and a
rebuilt interpreter can differ, so a fresh image will not match the hash
even when it was built from the same source.

A smoke test on the device:

1. boot recovery and confirm TWRP draws, with no adb;
2. `start-serial` (or `start-adb`) and read the console;
3. start Krux with volume-up, exercise QR/entropy and NFC/SeedKeeper;
4. Shutdown from Krux, watch the serial trace reach
   `wipe: powering off`.

---

## 9. Troubleshooting

| symptom | cause / fix |
|---|---|
| no adb after boot | expected in the sealed image; run `/sbin/start-adb` in the TWRP terminal |
| `adbd status 255` on sideload | `/sbin/recovery` is missing or not a symlink to `recovery.stock`; rebuild the ramdisk |
| sideload fails shortly after start | adbd was running and `minadbd` lost the USB endpoint race; boot fresh (adb off) or `setprop ctl.stop adbd`, then retry |
| TWRP commands return 255 | `/system/bin/sh` is missing; the ramdisk builder installs the symlink to `/sbin/sh` |
| `start-krux` does nothing | run it from TWRP's terminal or adb; it asks init to start the `krux-firmware` service, which stops TWRP first |
| device powers itself off after shutdown and comes back | USB charger; disconnect the cable |
| `dtb not found` from the bootloader | the image was built with `zImage`, not `zImage-dtb` |
| no display and no adb | use the UART shell (`start-serial`), or `fastboot boot` the image again |

The serial console is the reliable diagnostic path when adb is off.  The
kernel console owns `ttyHSL0`; `start-serial` gives a root shell on the
same line, with kernel messages interleaved.

---

## 10. Documentation

| document | contents |
|---|---|
| [docs/build-guide.md](docs/build-guide.md) | the build, layer by layer, with the current wrappers |
| [docs/build-kernel.md](docs/build-kernel.md) | the air-gap profile and the kernel build |
| [docs/memory-wipe-state.md](docs/memory-wipe-state.md) | the wipe design, the marker and the verified traces |
| [docs/security-audit.md](docs/security-audit.md) | release/seal/RAM plan and remaining items |
| [docs/packaging-plan.md](docs/packaging-plan.md) | the three delivery modes |
| [docs/directories.md](docs/directories.md) | the tree layout |
| [docs/README.md](docs/README.md) | the full documentation index |

The session history is in the git log, not in the tree.

---

## 11. The one rule

The project tree is the source; the device is a deployment.

- Edit the tree (`krux/src`, `cam/camd.c`, `krux/firmware/micropython`,
  `rootfs/init-stub.c`, `linux/`).
- Deploy or build; do not hand-edit files on the device.
- A device that has not been re-deployed holds an older tree, not a
  newer one.
