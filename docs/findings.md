# Standalone bring-up — findings

Everything below was measured on the device, not inferred. Where a claim rests
on a source tree rather than on hardware, it says so.

Full chronological detail, including every wrong turn, is in
[`airgap-standalone.log.md`](airgap-standalone.log.md) (69 entries).

---

## 1. Status

**All four hardware capabilities are demonstrated under a minimal freestanding
userspace** — our own `init` running as PID 1, no TWRP, no Android userspace,
no console, a ~4.8 KB initramdisk.

| capability | evidence | how it was observed |
|---|---|---|
| **screen** | magenta, green, text, countdown all render | human, on the panel |
| **touch** | two taps advance two stages, every run | human |
| **NFC** | `poll()` reports POLLIN, 6 bytes read, response begins `10 60` | on-screen text |
| **power-off** | device powers off after stage 6 | human |

Verified on **both** usable kernels (§3).

**Our own kernel now builds** (§13): CAF 3.4 `lineageos_hammerhead_defconfig`
+ the air-gap profile, gcc 4.9, out-of-tree, `zImage` 5.9 MB, 1101 options,
zero errors. It has `CONFIG_USB_FUNCTIONFS=y` — the thing all three existing
kernels lack — and no radio. It has **not yet been flashed**; the four
capabilities have not been re-verified on it.

**A new workstream is open: the camera** (§12, §14, §15).

Corrected on 2026-10-19 (§15): the device's cameras **work** — the rear is a
Sony **IMX179**, the front an OmniVision **MT9M114B**, both driven by the
camera_v2 subdev stack whose drivers *and* whose hammerhead device tree are
already in our tree. The real blocker is that camera_v2 has no userspace
support of its own: the kernel forwards every V4L2 request to a userspace
"imaging server" (`mm-qcamera-daemon` on Android) and blocks until it is
acknowledged. A replacement has to be written, using the standard V4L2
subdevice API.

Also learned: **`adb` works on the device** (§15.6) — the *installed* kernel
(`e4e483b4ef6`) has functionfs, unlike the three other kernels. That is a
second debugging channel alongside the framebuffer.

---

## 2. Hardware facts

| | |
|---|---|
| device | LG/Nexus 5 `hammerhead`, MSM8974 |
| RAM | 1,903,100 kB, 4 cores (`maxcpus=2` under TWRP) |
| panel | 1080×1920, **stride 4352 bytes**, `virtual_size 1080,3840` (two planes), 32 bpp |
| touch | `/dev/input/event1`, protocol B multitouch |
| NFC | `/dev/bcm2079x`, Broadcom BCM2079 / NFCI-HCI |

Notes that cost time to learn:

- **Stride 4352 is bytes, not pixels.** 32 bpp → a row is 1088 `u32` = 1088
  pixels, of which 1080 are visible. Sizing a layout against 4352 overflows.
- **The panel is command-mode DSI** (`dmesg`: "JDI 1080p command mode dsi
  panel"). Nothing scans out; a frame must be committed or the panel keeps
  showing whatever it had.
- **The CPU has an FPU.** `/proc/cpuinfo` reports `vfp edsp thumbee neon vfp3
  tls vfpv4`, and both kernels build with `CONFIG_VFP=y CONFIG_VFPv3=y
  CONFIG_NEON=y CONFIG_KERNEL_MODE_NEON=y`. The standing "everything must be
  soft-float" constraint rests on a premise that is **not true** — see §8.
- **No devtmpfs.** `/proc/filesystems` lists only `tmpfs` and `ext4`, so a
  minimal ramdisk starts with an empty `/dev`, and `mknod(2)` returns `EINVAL`
  to an unlabelled process. Device nodes are baked into the cpio archive as
  newc device entries instead.

Device node major:minor, all verified on the device:

```
/dev/graphics/fb0   crw-rw----  29,0    (display; open() powers the panel on)
/dev/input/event1   crw-rw----  13,65   (touch)
/dev/bcm2079x       crw-rw----  10,65   (NFC; O_NOCTTY is REQUIRED)
```

---

## 3. Kernels

There are **three** distinct kernels, not two. Every earlier claim of "the
kernel" conflated them.

| kernel | sha256 (16) | size | version | compiler | KSU | functionfs |
|---|---|---|---|---|---|---|
| ROM zip `boot.img` / `recovery.img` | `ffde091513d91018` | 7,383,264 | `3.4.113-🇺🇦-ge4e483b4ef6 #315` | **gcc 16.2.0** | no | off |
| recovery partition / `boot-kernelsu-20260926.img` | `a984548b32638807` | 7,414,992 | `3.4.113-🇺🇦-gda91d066bc6 #42` | gcc 15.2.1 Arm GNU 15.2.Rel1 | no | off |
| inside `twrp-3.7.0_9-HH...img` | `bbba49d13c789a8c` | 7,446,592 | *never characterised* | — | no | — |

Both the ROM's `boot.img` and its `recovery.img` ship the **same** kernel; only
the ramdisk differs.

### The file named `boot-kernelsu-20260926.img` is not a KernelSU kernel

```
kernel   a984548b32638807   == the recovery-partition kernel
ramdisk  25bbaa0f1b10a0ad   == the ROM zip's ramdisk
```

It is TWRP's kernel with LineageOS's ramdisk — a hybrid, and the one *without*
KernelSU. Its name and its contents disagree.

### KernelSU

The kernel **installed** in the boot partition (`e4e483b4ef6`, 1242 options)
carries it:

```
CONFIG_KSU=y  CONFIG_KSU_LSM_SECURITY_HOOKS=y
CONFIG_KSU_TAMPER_SYSCALL_TABLE=y
CONFIG_KSU_THRONE_TRACKER_ALWAYS_THREADED=y
CONFIG_KSU_HEURISTIC_IN_TREE_BUILD=y
```

Neither the ROM zip's kernel nor the TWRP kernel has a single `CONFIG_KSU`
symbol. `TAMPER_SYSCALL_TABLE` is the option that hides it from
`/proc/self/status`.

### The kernel tree we hold is NOT the tree these came from

`vendor/android_kernel_lge_hammerhead` contains **no KernelSU
anywhere** — `grep -rn 'config KSU' --include='Kconfig*'` returns zero hits.
The ROM was therefore built from a patched tree we do not have. This matters:
**reasoning about the running drivers from our tree is unsound**, and that
error produced a wrong conclusion twice.

### Configs

`/proc/config.gz` is readable on all kernels (`CONFIG_IKCONFIG_PROC=y`) and
`IKCONFIG_ST` is **not** findable in the compressed images — extraction from a
file does not work here (the CAF payload resists gzip/xz/bzip2/lzma/lz4/zstd,
and the kernel tree's own `scripts/extract-vmlinux` additionally requires an
ELF vmlinux, which CAF does not emit). Captures live in
`kernelcaps/`.

| config | options |
|---|---|
| ROM zip kernel | 1237 (`kernelcaps/android-kconfig`) |
| recovery kernel | 1213 (`kernelcaps/twrp-kconfig`) |
| installed Android kernel | 1242 (`kernelcaps/android-kconfig`) — **has KSU** |

`work/dev/kconfig` (1237) is the ROM zip kernel's config. Expanding each
defconfig and diffing against a live config:

| baseline | diff vs live recovery config |
|---|---|
| `lineageos_hammerhead_defconfig` | **113** |
| `hammerhead_defconfig` | 203 |

`lineageos_hammerhead_defconfig` is the device's base. Expansions kept in
`kernelcaps/defconfig-expansions/`.

### functionfs is off everywhere

All three configs say `# CONFIG_USB_FUNCTIONFS is not set`, and `/dev/ffs.ep0`
is absent — yet `/dev/usb-ffs/adb` exists. That directory is created by **adbd
itself**; the `ep0`/`ep1` endpoint files only ever come from `g_ffs`.

Consequence, measured: **adbd starts, finds no endpoint, and sits at `offline`
forever.** Toggling "ADB Sideload" in TWRP drives the same path and takes the
TWRP UI down. There is no legacy `f_adb` fallback in the tree.

This is a **one-symbol fix for a kernel we build** (`CONFIG_USB_FUNCTIONFS=y`)
and cannot be retrofitted onto an existing image.

---

## 4. Boot images

`fastboot boot` hands the image to the stock bootloader, which loads kernel and
ramdisk at the addresses **in the header**. Wrong addresses boot to nothing,
silently. `rootfs/bootimg.py` round-trips byte-identically and
refuses to invent a field.

```
kernel_addr  0x00008000      ramdisk_addr 0x02900000
tags_addr    0x02700000      page_size    2048
```

---

## 5. Display contract

```
open("/dev/graphics/fb0", O_RDWR)      -> powers the panel on by itself
mmap(len = stride * height * 2)         -> both planes
fill BOTH planes                        -> committing an unfilled plane = blank screen
ioctl(fd, 0x40A86DA4, &mdp_display_commit) with yres_virtual=3840, yoffset=0
```

`0x40A86DA4` is `_IOW(MSMFB_IOCTL_MAGIC, 164, struct mdp_display_commit)` with
`MSMFB_IOCTL_MAGIC == 'm'`.

**`struct fb_var_screeninfo` is 160 bytes; `struct mdp_display_commit` is 168.**
Getting this wrong is silent and dangerous: the driver copies the full 168-byte
`var` *back* to userspace, so a 120-byte object means the kernel writes 48 bytes
past ours. It appears to work only because every field the commit path reads
sits before `grayscale`. `main.c` carries compile-time tripwires on both sizes.

---

## 6. NFC contract

The two fixes that made it work both came from reading
`drivers/nfc/bcm2079x.c`, not from the HAL:

```c
/* read() is a SINGLE i2c_master_recv of 4 bytes. It does not block and does
 * not wait for the controller -- it returns 0 if the chip has not answered. */
ret = i2c_master_recv(client, tmp, 4);
if (ret == 4) { /* parse length, read the remainder */ }
return total;

/* poll() reports readable only once the chip has pulled its interrupt line */
if (bcm2079x_dev->count_irq > 0) { count_irq--; mask |= POLLIN; }
```

Working sequence:

```c
open("/dev/bcm2079x", O_RDWR | O_NOCTTY)   /* O_NOCTTY is mandatory */
ioctl(fd, POWER_CTL, 1);                   /* EN high */
sleep_ms(100);                              /* SETTLE -- without this, write is EIO */
write(fd, core_reset);                     /* {0x10,0x60,0x00,0x02,0x00,0x01}, retry x4 */
write(fd, get_build);                      /* {0x10,0x2F,0x04,0x00},      retry x4 */
poll(fd, POLLIN, 500);                      /* WAIT FOR THE IRQ */
read(fd, rsp, 64);                          /* -> 6 bytes beginning 10 60 */
```

**Do not assert `WAKE_CTL` during bring-up.** `b/nfc/TRANSPORT.md` records the
working HAL's state as `NFC_WAKE_ASSERTED_ON_POR == UPIO_OFF` — wake is
deasserted at power-on.

Failures the driver reports, and what they mean:

| errno | meaning |
|---|---|
| `EIO` (5) | `i2c_master_send` returned short — usually "chip not ready yet" |
| no `POLLIN` | the chip never raised its IRQ at all |

---

## 7. Power-off contract

```c
reboot(LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_POWER_OFF, 0);
```

ARM's argument order is **magic first, command third** (`kernel/sys.c:455`).

```
LINUX_REBOOT_MAGIC1        0xfee1dead
LINUX_REBOOT_MAGIC2        0x28121969
LINUX_REBOOT_CMD_POWER_OFF 0x4321FEDC     <- NOT 0x4321
```

`0x4321` matches no case in the switch, falls through to `default`, and returns
`-EINVAL`. That, plus the struct bug in §5, is what made stage 7 fail across
several runs.

`pm_power_off` is **not** NULL on this platform: `arch/arm/mach-msm/restart.c:367`
sets it whenever `MACH_MSM8974_HAMMERHEAD=y`, which is true in every kernel
here.

Constants come from the kernel's own header, vendored at
`rootfs/include/linux/reboot.h`, and
`verify-constants.py` checks that vendored copy against the tree plus every
other hand-transcribed value (24 checks).

---

## 8. Toolchains

| toolchain | version | where |
|---|---|---|
| AOSP prebuilt | **gcc 4.9.x 20150123** | `prebuilts/gcc-androideabi-4.9/bin/` |
| distro cross | gcc 15.2.0 | `arm-linux-gnueabihf-gcc` on `PATH` |
| AOSP clang | r614150 | `prebuilts/clang-linux-x86/clang-r614150` |
| buildroot host | gcc 15.3.0 | `rootfs/build/host/` (7.6 GB, regenerable) |

The kernels actually running were built with **gcc 16.2.0** (ROM zip) and
**Arm GNU 15.2.Rel1** (recovery) — neither is in the project. The ROM author
reports Linaro for the Android kernel; the device itself reports gcc 16.2.0.

### What is actually needed, and what is just taking up room

| directory | size | needed? | why |
|---|---|---|---|
| `prebuilts/gcc-androideabi-4.9` | 136 MB | **yes** | the working toolchain. `bin/arm-linux-androidkernel-gcc` is a symlink to `arm-linux-androideabi-gcc`, whose wrapper resolves its real compiler by its own filename (`real-` + basename of the readlink target) — so the alias works, and `real-arm-linux-androideabi-gcc` is what actually runs. The tree is **not** in a system location, so the build must put its directory on `PATH` itself |
| `prebuilts/clang-linux-x86` | **8.3 GB** | **no** | neither running kernel used clang (gcc 16.2.0 and Arm GNU 15.2.Rel1). `build-kernel.sh` has an LLVM path, but the gcc path is the one in use. This is the single largest reclaimable item in the project |
| `rootfs/arm-linux-androideabi-4.9` | 422 MB | **no** | an **incomplete** copy of the same AOSP 4.9 prebuilt: `bin/` holds 34 binutils/gcov/gprof binaries but **no gcc driver**, and nothing in the project references it (`build-kernel.sh` only searches `$PREBUILTS/…`, and `$PREBUILTS` is the project-root `prebuilts/`, not `rootfs/`) |
| `rootfs/buildroot` | 1.4 GB | moved | buildroot **source** (2026.09). Relocated to `buildroot` so everything build-related sits under `` |
| `rootfs/build` | 7.6 GB | no | buildroot **output** — host toolchain and target. Regenerable, and the host gcc there (15.3.0) is not used by the kernel build |

The `rootfs/arm-linux-androideabi-4.9` case is the one to watch: it looks
complete enough to use and it is not. It has binutils but no compiler, so
anything that tried to use it would fail at the first `.c` file, not at a
check.

### The soft-float mandate is not load-bearing

The constraint "every binary must be `-mfloat-abi=soft` because `AT_HWCAP_FP` is
clear" does not survive contact with the hardware:

```
Features : swp half thumb fastmult vfp edsp thumbee neon vfpv3 tls vfpv4 idiva idivt
```

The CPU has VFPv3/VFPv4, and **both** kernels build with
`CONFIG_KERNEL_MODE_NEON=y`, which means the kernel itself emits VFP/NEON
instructions. If FPU trapped, nothing on this device would run.

Our payload is soft-float anyway (0 VFP instructions, verified), which is
harmless. What genuinely matters is not mixing ABIs within one image — there is
nothing to mix, since we are a freestanding static binary. **Re-check
`AT_HWCAP` before treating this as settled.**

---

## 9. Tooling bugs found

In `build/build-kernel.sh`:

| bug | effect |
|---|---|
| `CROSS_COMPILE` gets a doubled dash (`arm-linux-androidkernel--`) | `CC` resolves to a **missing** binary; fails at the first C file, not at `defconfig`. **Note the dash itself is required** — the kernel does `CC = $(CROSS_COMPILE)gcc` and inserts nothing — so the fix is to delete the *second* dash, not the first |
| calls `make olddefconfig` | does not exist in 3.4 (arrived ~3.16); aborts the profile check, so even `--config` cannot finish. `silentoldconfig` exists but still **prompts** for symbols it has not seen, and then aborts with `(NEW) aborted!`. 3.4's answer is `oldconfig` with stdin closed: every prompt takes its default, exit 0 |
| defconfig glob `*hammerhead*defconfig` | matches `hammerhead_defconfig` first; builds the **wrong** baseline, 203 lines from the device instead of 113. Replaced with an explicit preference list so the choice is visible |
| prefers gcc 4.9 | not what built either running kernel (gcc 16.2.0 / Arm GNU 15.2.Rel1) |
| `tools/profiles/hammerhead.list` removes `CONFIG_NFC` and `CONFIG_NFC_BCM2079X` | **contradicts `build/README.md`**, which says NFC is deliberately kept. Would delete the driver proven working in §1. Fixed: the symbols are gone from the profile |
| `CONFIG_MSM_IPC_ROUTER` enabled on device, absent from the profile | QMI transport framework survives — **deliberate**, see the profile's own comment: the SMD channel also carries WLAN/GNSS/sensor/DSP traffic |
| the toolchain directory is not put on `PATH` | `CROSS_COMPILE` only names the *prefix*; the kernel then execs `$(CROSS_COMPILE)gcc` by bare name, so the first real compile dies `/bin/sh: 1: arm-linux-androidkernel-gcc: not found`, Error 127. The script found the compiler by absolute path for its version report and still could not compile |
| the toolchain report printed `arm-linux-androidkernel--gcc` | cosmetic only — the doubled dash was never passed to make — but it looked like the very bug above and was worth removing so it cannot be mistaken for one |

In `tools/apply-defconfig.py`:

| bug | effect |
|---|---|
| `strip_defconfig` **deleted** the line and let the symbol fall back to its Kconfig default | safe only when the default is `n`. For a symbol whose default is `y` — `CONFIG_WLAN` is literally `menuconfig WLAN … select WIRELESS … default y` — deleting the line is a request for it. **10 of 79** profile symbols came back enabled: `WLAN`, `WIRELESS`, `MSM_RMNET`, `MODEM_SUPPORT` and six `USB_NET_*` variants. Fixed: write `# CONFIG_X is not set`, which forces `n` regardless of the default |
| the config re-resolution ran `make -C <kernel_root>` with no `O=` | the build is out-of-tree, so the config it had just written was not where make looked: `*** Configuration file ".config" not found!` |
| two string-valued symbols in the profile | `CONFIG_RMNET_SMD_CTL_CHANNEL` / `_DATA_CHANNEL` are device paths, not booleans — `# … is not set` is not a valid state for them, and they are already `""` on the device. The audit flagged them as re-enabled forever. Removed from the removal list |
| `apply-defconfig.py` could only remove | a stripped kernel with no adb is undebuggable, so `CONFIG_USB_FUNCTIONFS` has to be **added**. Added `--enable` plus a keep-list (`tools/profiles/hammerhead-keep.list`), and the tool refuses a symbol that appears in both lists rather than silently resolving the contradiction |
| the keep-list was read with plain `while read` | `read` **drops the final line** when the file has no trailing newline, and these files are hand-edited. The keep-list had no trailing newline, so its only symbol was silently skipped and functionfs never reached the config. Fixed with `while read … || [ -n "$sym" ]` |

### 9a. `CONFIG_USB_FUNCTIONFS` is a `choice` value

The most expensive bug in this list, and the one most likely to recur.

`drivers/usb/gadget/Kconfig:552..991` is

```
choice
	tristate "USB Gadget Drivers"
	default USB_ETH
	...
	config USB_FUNCTIONFS
		tristate "Function Filesystem (EXPERIMENTAL)"
		depends on EXPERIMENTAL
	...
endchoice
```

So `USB_FUNCTIONFS` is **mutually exclusive** with `USB_G_ANDROID`, `USB_ETH`,
`USB_MASS_STORAGE` and 26 others. Writing `CONFIG_USB_FUNCTIONFS=y` is not
enough: kconfig lets the **last** choice member it reads in `.config` win, so
the request is silently discarded in favour of the existing
`CONFIG_USB_G_ANDROID=y`, and the only trace is

```
.config:2730:warning: override: USB_G_ANDROID changes choice state
```

A warning, not an error — the build proceeds and the feature is simply absent.
`apply-defconfig.py` now parses the tree for `choice` blocks and clears all 29
siblings, which is what makes the enable stick.

The cost of winning the choice is real and deliberate: `USB_G_ANDROID`,
`USB_ETH`/`RNDIS` and `USB_MASS_STORAGE` are all gone. On an air-gapped
signing device that is the correct trade — the only USB function we want is
one a local process drives. The USB **controller** menu (`USB_GADGET`, i.e.
DWC3/MSM) is a different block and is untouched.

Two things that are *not* evidence of functionfs existing, both checked
against live kernels: `/dev/usb-ffs/adb` (adbd creates that directory
itself) and the mere presence of `CONFIG_USB_GADGET`. The real test is
`/dev/ffs.ep0`.

---

## 10. Where things live

```
airgap-standalone.log.md            the full log, 69 entries
STANDALONE-FINDINGS.md              this file
CONTINUE-PROMPT.md                  how to pick this up again


  kernelcaps/                       live config captures + defconfig expansions
  patches/                          our kernel-tree patches (0001 f_fs, 0002 ov5640)
  vendor/krux/                     Krux @ 9b3a431 -- read, not assumed
  vendor/micropython/              MicroPython @ 9a3a0c0
  vendor/android_kernel_lge_hammerhead/   CAF 3.4 kernel tree @ 9ac11678
  nfc/                              transport contract, verified headers
  phase0/                           earlier probes (pre-standalone)
  tools/devnodes.py                 newc cpio builder -- device nodes baked in
  extract-kconfig.py                config-from-image (does not work on CAF; kept)
  capture-kernel.sh                 the capture script used for §3
  TOOLCHAIN.md
  vendor/
    android_kernel_lge_hammerhead/   the kernel tree (704 MB)
    micropython/                     MicroPython at 9a3a0c0 (63 MB)
  rootfs/
    bootimg.py                      verified boot.img packer/unpacker
                         the payload that passes all seven stages
      main.c                        the whole thing, heavily commented
      include/linux/reboot.h        vendored from the kernel tree
      font5x7.h                     generated by gen-font.py
      gen-font.py                    rasterises the font from a TrueType face
      verify-constants.py            24 checks against the kernel tree
    stock/                          built images
```

### The payload

`rootfs/` builds to a **4.8 KB** initramdisk whose `init`
is a freestanding ARM binary — no libc, no `/bin/sh`. Seven stages:

```
1  boot as PID 1, no TWRP, no Android
2  screen  -> magenta
3  wait for touch      (flashing magenta, 1 Hz heartbeat)
4  NFC -> Core Reset + GET_BUILD_INFO
5  screen  -> green, then 90 s of TEXT holding the NFC result
6  wait for touch      (flashing green)
7  power off           -> device shuts down
```

The screen is the only output channel. Results are drawn as **text**, not bars:
an earlier design encoded values as bar counts and produced six different
misreadings in six runs (a popcount read as a count, a hard-coded placeholder
read as an errno, a hexdump read as a step number). A 5×7 font costs 320 bytes
and removes the entire class of error.

---

## 11. Build and flash

```sh
cd rootfs/standalone
python3 gen-font.py                    # regenerate font5x7.h
python3 verify-constants.py            # 24 checks, exit 1 on any mismatch
arm-linux-gnueabihf-gcc -O2 -static -nostdlib -ffreestanding \
    -fno-stack-protector -fno-asynchronous-unwind-tables \
    -mfloat-abi=soft -marm -march=armv7-a -mtune=cortex-a9 -Iinclude -o init main.c

cd ..
python3 bootimg.py pack stock/unpacked ramdisk.cpio.gz \
    stock/boot-standalone2.img           # recovery kernel
python3 bootimg.py pack <rom-unpacked>  ramdisk.cpio.gz \
    stock/boot-standalone-romkernel.img  # ROM zip kernel
```

```sh
adb reboot bootloader
fastboot boot b/rootfs/stock/boot-standalone2.img
```

`fastboot boot` is temporary — it writes nothing to the boot partition.
Recovery if it hangs: Vol+ + Power into fastboot.
---

## 12. The camera (Krux requirement)

> **SUPERSEDED IN PART BY §15.** Read §15 first. The facts in §12 that are
> still true: there is no V4L2 for this sensor, the ABI is userspace-
> includable, there is no ISP firmware blob, the camera stack compiles.
> The facts in §12 that are **wrong**: the rear camera is an **IMX179**, not
> an OV5640; the ROM's camera **works**; hammerhead's camera config **is** in
> the tree (as a DT); and the "capture client" does not talk V4L2 to the
> sensor node directly, because camera_v2 requires a userspace imaging
> server in between.

Added 2026-10-19 because Krux needs the camera for two things: reading QR codes
(BBQr, UR, animated UR) and generating entropy from still images. Both need
raw frames at a resolution Krux chooses, a few frames per second for animated
UR and for the entropy-image preview.

Everything below was **read out of the trees on disk**, not recalled. The
conclusion is the opposite of the initial assumption in both directions.

### 12.1 There is no V4L2 for this sensor — and that is not a gap

`drivers/media/platform/msm/camera_v1/Kconfig:1`:

```
config MSM_CAMERA_V4L2
	bool "MSM Camera V4L2 Interface"
	depends on MSM_CAMERA
	---help---
	  This flag enables V4L2 interface of MSM camera driver. If enabled,
	  application interacts with /dev/video0 through V4L2 APIs. Otherwise,
	  native APIs are used through /dev/config0, /dev/frame0, and /dev/control0.
```

And `camera_v1/Kconfig:39`:

```
config OV5640
	bool "Sensor OV5640 (YUV 5M)"
	depends on MSM_CAMERA && !MSM_CAMERA_V4L2
```

The Nexus 5's sensor is the OmniVision **OV5640** (5 MP, YUV). It lives in
**camera_v1**, and it explicitly *refuses* to coexist with the V4L2 interface.
The V4L2 path is camera_v2, whose sensor list is
`imx135, imx179, mt9m114, ov2720, ov8825, ov9724, s5k3l1yx` — **no OV5640 at
all**. So enabling `MSM_CAMERA_V4L2` would give a `/dev/video0` and no sensor.

Your premise was right, and the reason is now precise: on this hardware the
standard V4L2 path is a different sensor generation, not a missing driver.

The real interface is Qualcomm's **native** one: `/dev/config0`,
`/dev/control0`, `/dev/frame0` (and `/dev/pic0`).

### 12.2 The good news: the native ABI needs no blob

`include/media/msm_camera.h` in our own tree is a **userspace-includable**
header — it pulls in `linux/videodev2.h` and `linux/ioctl.h` and defines the
complete ioctl set (`MSM_CAM_IOCTL_CONFIG_VFE`, `_ENABLE_VFE`, `_GETFRAME`,
`_RELEASE_FRAME_BUFFER`, `_SENSOR_IO_CFG`, `_GET_CAMERA_INFO`, … about 70 of
them). This is the same header Qualcomm's `libcamera.so` HAL compiles
against, so it is a stable, documented-in-source contract.

And there is **no firmware to load**: `grep -r request_firmware
drivers/media/platform/msm/camera_v1/` returns **nothing**. The ISP microcode
is in the kernel (`msm_isp.c`, `msm_vfe40.c`).

So the capture path is: write a small client that opens the three nodes and
drives the same ioctl sequence `libcamera.so` does. No vendor blob is
required for raw frames. The blob is what implements the *rest* of a camera
stack — 3A control, JPEG encode, preview scaling, HAL plumbing — and none of
that is needed to read a QR code.

### 12.3 The bad news: the ROM on the device has no camera

The LineageOS 23.2 hammerhead port we boot has this in its kernel config:

```
# CONFIG_MSM_CAMERA is not set      <- camera_v1 (the one with OV5640) is OFF
CONFIG_MSMB_CAMERA=y                <- camera_v2 is ON
CONFIG_MSM_CAMERA_SENSOR=y
CONFIG_MSM_CPP=y  CONFIG_MSM_CCI=y  CONFIG_MSM_CSIPHY=y
CONFIG_MSM_CSID=y  CONFIG_MSM_ISPIF=y
CONFIG_IMX179=y
```

There is **no `CONFIG_OV5640`** anywhere in it, and camera_v2 has no OV5640
driver. The port enabled the modern stack, which cannot see this phone's
sensor.

Three consequences:

1. **"Start in Android and see which blobs we need" does not work as
   stated.** There is no camera to learn from on the Android side.
2. **The ROM's camera blobs are the wrong ABI anyway.** The modern HAL talks
   to camera_v2; we need the camera_v1 native ABI. Borrowing them would not
   help.
3. **This argues for our own kernel as the base, not against it.** The CAF
   3.4 tree we already vendor is the *only* tree on this disk that has a
   driver for the Nexus 5's sensor.

### 12.4 The camera stack compiles — verified, not assumed

`CONFIG_MSM_CAMERA` is `default y` in `drivers/media/platform/msm/Kconfig:7`
but is **off in both defconfigs** we have, so it had never been built here.
Turning it on:

```
CONFIG_MSM_CAMERA=y
CONFIG_OV5640=y
CONFIG_MSM_CAMERA_SENSOR=y
CONFIG_MSM_ACTUATOR=y
CONFIG_MSM_EEPROM=y
CONFIG_MSM_CAMERA_FLASH=y
CONFIG_MSM_CAMERA_AF_FOXCONN=y     (auto-selected; the hammerhead AF module)
```

`make drivers/media/platform/msm/camera_v1/` now succeeds — **20 objects,
exit 0** — after a two-line fix to `ov5640.c` (it was missing
`<linux/module.h>` and `<linux/gpio.h>`; see
`patches/0002-camera-ov5640-missing-includes.patch`).

One hygiene item: enabling `MSM_CAMERA` also turns on `MT9D112`, `MT9P012`,
`MT9P012_KM`, `MT9T013` and `S5K3E2FX`, because they are `default y` in
camera_v1/Kconfig. They are for other boards, and the camera probes the
sensor over I2C by trying the enabled drivers until one answers. Only
`OV5640` should be enabled.

### 12.5 What is genuinely missing: hammerhead's camera configuration

This is the real gap, and it is **data, not code**.

CAF 3.4 keeps the camera's pin/clock/power description in **userspace**: the
HAL holds it and pushes it down with `MSM_CAM_IOCTL_SENSOR_IO_CFG`
(`msm_camera.c:2837` → `sctrl.s_config`). The kernel's `ov5640_probe_init_gpio()`
reads `data->sensor_pwd`, `data->sensor_reset` and `data->vcm_pwd` from
exactly that structure.

Our tree has camera board files for 8064, 8930, 8960, 7627a, 7x30 and 8x60 —
but **no `board-8974-camera.c`**. Hammerhead's camera configuration (sensor
GPIOs, `cam_vreg`/`cam_vdd` regulators, `cam_clk`/`mclk`, the camera I2C bus
and slave address) is therefore not in the tree and has to be supplied:

- either as a `board-8974-camera.c` we write, or
- by pushing `struct msm_camera_sensor_info` from our own userspace, which is
  the mechanism the HAL used anyway and is the better fit for us, because we
  own the userspace.

Authoritative values are obtainable from the original CAF/LGE hammerhead
camera config (an older CM11/LineageOS hammerhead source) — the *data*, not
the code.

### 12.6 The remaining work, in order

| # | item | where | risk |
|---|---|---|---|
| 1 | kernel config: `MSM_CAMERA`, `OV5640`, other sensors off | `tools/profiles/hammerhead-keep.list` (needs an "enable" side that the stripper now has) | low — compiles today |
| 2 | camera patches: `ov5640.c` includes | `patches/0002-*` | done |
| 3 | device nodes `/dev/config0`, `/dev/control0`, `/dev/frame0` | `tools/devnodes.py` — **no devtmpfs**, so they must be baked into the cpio like fb0/event1/bcm2079x | low, but the *numbers* (node 0,1,2,3) are chosen by the driver, so they must be right |
| 4 | hammerhead camera config (GPIOs, regulators, clocks, I2C) | new file, or pushed from userspace | **medium** — wrong values = no frames, and there is no easy way to probe them without a working capture |
| 5 | native-API capture client (C, static) | new | medium — the ioctl sequence and the VFE/ISP parameter block are the fiddly part |
| 6 | get raw YUV frames out, convert, blit to the framebuffer | extends `rootfs/main.c` | low — and the framebuffer is a *perfect* observable: we can literally see the camera image on the panel |
| 7 | MicroPython/C binding for Krux | depends on 5 | medium |
| 8 | **QR decoding performance on a Cortex-A9** | Krux side | **unknown, and the real risk** — the decoder, not the camera, is the bottleneck |

Items 1–3 are cheap and can be done alongside the kernel work. Items 4–5 are
the substance. Item 8 is a separate question that the camera does not
decide: a few frames per second of *capture* is plausible at a reduced
resolution, but if the QR decoder is pure Python the decode rate may be the
binding constraint. Worth measuring before committing to a camera
architecture.

### 12.7 Two things to keep in view

- **A camera in a signing device is a privacy and attack-surface question,
  not just a feature.** It is a sensor pointed at the user's environment, and
  a QR code is untrusted input to a device that signs. Krux's use — entropy
  from images — is a legitimate reason, but it is worth deciding explicitly
  rather than by default, because the camera is the one component in this
  project that can observe the room.
- **The camera does not weaken the air gap.** It is not a radio. But it does
  pull in ION/memory-allocation machinery, so the air-gap profile should be
  re-audited once `MSM_CAMERA` is enabled (the profile's own verification
  step will do that automatically).

---

## 13. Our own kernel — the build that works

Built 2026-10-19. Command:

```sh
./build/build-kernel.sh \
  --tree "$PWD/vendor/android_kernel_lge_hammerhead" \
  --out /tmp/opencode/ko
```

Result: `zImage` 5,938,864 bytes, **1101 options, zero errors**.

| item | value |
|---|---|
| base | `arch/arm/configs/lineageos_hammerhead_defconfig` (chosen by explicit list, not a glob) |
| toolchain | AOSP gcc 4.9.x 20150123, `prebuilts/gcc-androideabi-4.9/bin/arm-linux-androidkernel-gcc` (a symlink to the androideabi wrapper, which resolves its own real compiler by name) |
| output | out-of-tree, `O=/tmp/opencode/ko` |
| patches | `patches/0001-usb-gadget-f_fs-include-uidgid.patch` — `f_fs.c` was missing `#include <linux/uidgid.h>`; the tree *has* `include/linux/uidgid.h` (with `kuid_t`, `make_kuid`, `uid_valid`) but `f_fs.c` never included it, and `linux/fs.h` does not either. It only surfaced now because functionfs was previously never built. |

Config facts, verified in `/tmp/opencode/ko/.config`:

| symbol | state |
|---|---|
| `CONFIG_USB_FUNCTIONFS` | **y** (this is the whole point — all three existing kernels have it off) |
| `CONFIG_USB_FUNCTIONFS_GENERIC` | y (auto-selected) |
| `CONFIG_BCMDHD` / `CONFIG_WLAN` / `CONFIG_WIRELESS` / `CONFIG_BT` | off |
| `CONFIG_MSM_RMNET` / `CONFIG_MODEM_SUPPORT` / `CONFIG_USB_NET_QMI_WWAN` | off |
| `CONFIG_NFC_BCM2079X` | **y** (survives the profile — the profile no longer lists it) |
| `CONFIG_FB_MSM` / `CONFIG_TOUCHSCREEN_LGE_SYNAPTICS` | y |
| `CONFIG_MACH_MSM8974_HAMMERHEAD` / `CONFIG_BLK_DEV_INITRD` | y |

`CONFIG_USB_FUNCTIONFS` is a **choice value** in the "USB Gadget Drivers"
choice, so enabling it meant clearing 29 siblings (§9a). The cost:
`USB_G_ANDROID`, `USB_ETH`/`RNDIS` and `USB_MASS_STORAGE` are gone. On this
device that is the right trade — the only USB function wanted is one a local
process drives. The USB *controller* (`USB_GADGET`) is untouched.

### Not yet done

- **Not flashed.** `fastboot boot` with this zImage is the next step, and
  the four capabilities must be re-verified on it.
- `adb devices` reaching `device` rather than `offline` is unproven — that
  was the reason for the functionfs work in the first place.
- `AT_HWCAP` still needs re-checking on our own kernel before the
  hosted-vs-freestanding MicroPython decision (see §8).

---

## 14. What Krux actually needs from the camera

Krux vendored at `vendor/krux` (commit `9b3a431`, 2026-10-07).
Read, not assumed.

### 14.1 It is the OpenMV `sensor` + `image` API, not a generic camera API

`src/krux/camera.py:25` is `import sensor`, and the whole camera layer is
written against **MicroPython's OpenMV `sensor` module**:

```python
sensor.reset(freq=18200000)
sensor.get_id()
sensor.set_pixformat(sensor.RGB565)     # or sensor.GRAYSCALE
sensor.set_framesize(sensor.QVGA)        # or sensor.VGA
sensor.set_windowing((0, 0, 240, 240))
sensor.set_hmirror(1); sensor.set_vflip(1)
sensor.run(0) / sensor.run(1)
sensor.snapshot()
sensor.skip_frames()
sensor.__write_reg(0x56, 0x0)            # DIRECT sensor register writes
```

`snapshot()` returns an **OpenMV `image.Image`**, and the API surface Krux
actually touches is small:

| call | used by |
|---|---|
| `img.find_qrcodes(find_inverted=bool)` | `pages/qr_capture.py:197` — **the QR decoder** |
| `img.get_statistics()` → `.l_stdev() .a_stdev() .b_stdev()` | `pages/capture_entropy.py:95` |
| `img.to_bytes()` | `capture_entropy.py:196` (16-bit pixels, fed to `shannon.entropy_img16b`) |
| `img.width()`, `img.height()` | several |
| `img.draw_string()`, `img.draw_rectangle()`, `img.find_blobs()`, `img.get_histogram()`, `img.lens_corr()` | QR viewing / m5stickv only |

So a MicroPython port for this device must provide **five** native modules:

| module | what it is | where it comes from |
|---|---|---|
| `sensor` | camera control + capture | we write it (OpenMV's is K210/ESP32-specific) |
| `image` | the image object, incl. `find_qrcodes` | we write it |
| `shannon` | `entropy_img16b(bytes)` | OpenMV C module; not vendored in Krux (only a simulator mock) |
| `qrcode` | QR **encoder**, for showing codes | `src/krux/qr.py:25`, `src/krux/pages/qr_view.py:23` — `import qrcode` |
| `lcd` | `lcd.display(img, oft=..., roi=...)` | board-specific; ours would be the framebuffer |

**The QR decoder is the hard part and it is not optional.** OpenMV implements
`find_qrcodes` with **quirc**. `quirc` is BSD-licensed, self-contained C
(~7k lines, no dependencies) and is *the* decoder already proven on exactly
this use case. `zbar` is more robust but much larger; `zxing-cpp` is C++ and
out of proportion. **quirc is the choice** — and it is a straight port into
our MicroPython port as a native module.

### 14.2 The OV5640 is not in Krux's sensor table — and that silently breaks two features

`src/krux/camera.py:29-32` knows exactly four sensors:

```python
OV2640_ID = 0x2642
OV7740_ID = 0x7742
GC0328_ID = 0x9D
GC2145_ID = 0x45
```

The Nexus 5's OV5640 reports **0x5640**, which is in none of them. The
consequences are *silent*, not errors:

- `config_map.get(self.cam_id)` returns `None` → `luminosity_threshold()`
  does nothing → the sensor's AEC/luminity window is never configured.
- `has_mode_control()` returns `False` → `qr_capture.py:167` takes the
  `else` branch and **exits the capture loop** instead of toggling
  anti-glare mode. Anti-glare is a *scan-a-QR-off-a-screen* feature, and
  this is exactly the case it exists for.
- `zoom_mode()` has no branch for 0x5640 → **zoom does nothing**. Zoom exists
  to read a partially-visible QR, which is the "BBQr" and "UR" use case.

So a patch to `camera.py` is required: add `OV5640_ID = 0x5640` and give it
the OV2640-style treatment. The OV2640 and OV5640 are both OmniVision parts
sharing the bank-select (`0xFF`), luminance (`0x24`/`0x25`/`0x26`) and
windowing registers Krux writes, so the existing `config_ov_2640` path is
close enough to be worth trying first — but this needs **verifying against
the OV5640 register map in our own tree**
(`drivers/media/platform/msm/camera_v1/ov5640.c` has the full register table).

### 14.3 `sensor.__write_reg` means we need raw I2C to the sensor

Krux writes sensor registers directly, per sensor model. Our `sensor` module
must therefore expose a raw register write, which is easy — the camera's I2C
bus is right there — but it means the `sensor` module cannot be a thin
wrapper; it needs the sensor's I2C client.

### 14.4 The kernel must give us devtmpfs, because the camera's major is dynamic

`msm_camera.c:4007`:

```c
rc = alloc_chrdev_region(&msm_devno, 0, 4 * MSM_MAX_CAMERA_SENSORS, "msm_camera");
```

The camera's char-device major is **allocated at runtime**. The node *names*
are fixed (`/dev/control0`, `/dev/config0`, `/dev/frame0`, `/dev/pic0`, from
`device_create(..., "%s%d", suffix, node)`), but the major is not knowable
at build time.

Our current technique is to bake the device nodes into the initramfs cpio
(`tools/devnodes.py`), which works only for **static** majors.

`CONFIG_DEVTMPFS` is available in our tree (`drivers/base/Kconfig:24`, needs
`HOTPLUG`, which is `y`), and is off in our config today — as it is in every
existing kernel, which is why "no devtmpfs" was recorded as a hardware fact.
**We can turn it on in our own kernel**, and then:

- the camera nodes appear with the right dynamic major, automatically;
- `/dev/ffs.ep0` (functionfs) appears too, for the same reason;
- and the baked nodes for fb0 / event1 / bcm2079x become redundant.

This is a small, high-leverage kernel change that the camera requirement
forces anyway. It should be done **before** the camera work, not after.

### 14.5 The plan

**Phase A — kernel (small, mostly done).**

1. Enable `CONFIG_DEVTMPFS=y` + `CONFIG_DEVTMPFS_MOUNT=y`.
2. Enable the camera: `CONFIG_MSM_CAMERA=y`, `CONFIG_OV5640=y`,
   `CONFIG_MSM_CAMERA_SENSOR=y`, `CONFIG_MSM_ACTUATOR=y`,
   `CONFIG_MSM_CAMERA_FLASH=y` — and explicitly **disable** the other
   sensors (`MT9D112`, `MT9P012`, `MT9P012_KM`, `MT9T013`, `S5K3E2FX`), which
   are `default y` in camera_v1/Kconfig and would otherwise be probed on the
   camera's I2C bus. The air-gap profile's own verification step will
   re-audit the result.
3. Keep the two patches (`patches/0001-*`, `0002-*`).

**Phase B — the camera bring-up, in our own payload before any MicroPython.**

Write a small static C program that drives the native API
(`/dev/config0`, `/dev/control0`, `/dev/frame0`) the way `libcamera.so`
does — `MSM_CAM_IOCTL_GET_CAMERA_INFO` → `_SENSOR_IO_CFG` (this is where
hammerhead's sensor GPIOs/regulators/clocks go) → `_CONFIG_VFE` →
`_ENABLE_VFE` → `_GETFRAME` → mmap → copy → `_RELEASE_FRAME_BUFFER` — and
then **blit the captured frame to the framebuffer**. The panel is a perfect
observable: if the camera works, we literally see the room on the Nexus 5's
screen. That also settles the question of whether hammerhead's camera config
is right, with no adb and no serial console in the loop.

**Phase C — the MicroPython native modules.** `sensor` first (a thin wrapper
over Phase B's client), then `image` + quirc, then `shannon` and `qrcode`.

**Phase D — Krux.** Patch `camera.py` for `OV5640_ID`, then wire up
`qr_capture.py` and `capture_entropy.py`.

### 14.6 The two architecture options for Phase C

| | **A. OpenMV-compatible** | **B. Native-first, patch Krux** |
|---|---|---|
| what | implement `sensor`, `image` (with `find_qrcodes`), `shannon`, `qrcode`, `lcd` | one C module (`msmcam`) with `capture()` / `decode_qr()` / `encode_qr()`; patch `camera.py` and `qr_capture.py` to use it |
| Krux | runs upstream (plus the OV5640 patch) | becomes a fork |
| work | implements `image` methods Krux never calls here (`draw_*`, `find_blobs`, `lens_corr`, `get_histogram`) | those are simply not written |
| risk | low divergence, but a larger surface to get right | more divergence, but a much smaller surface |

**Recommendation: B, with A's `sensor` module shape.** The `sensor` module is
cheap (it is a wrapper over the Phase B client) and keeps `camera.py` almost
untouched — only the OV5640 id needs adding. But `image` does not need to be
a general OpenMV clone: it needs `find_qrcodes`, `get_statistics`,
`to_bytes`, `width`, `height`, and whatever `lcd.display` consumes. Skipping
`draw_*`/`find_blobs`/`lens_corr`/`get_histogram` removes most of the work.

### 14.7 Performance — the thing to measure, not assume

Capture at QVGA (320x240) is what Krux asks for, and the Nexus 5's Cortex-A9
is several times faster than the K210 or ESP32 in the OpenMV boards Krux
targets. But **quirc is not a fast decoder**, and it is pure C running
single-threaded on a Cortex-A9 at 320x240.

The unknown is the *decode* rate, not the *capture* rate. The honest way to
settle it: build a quirc prototype, feed it still images of a QR code at
320x240, and time it on the device. If quirc is too slow, the alternatives
are zbar (bigger, faster) or decoding at a lower resolution.

Note the dependency: this measurement is only possible once Phase B works,
i.e. once we can capture a frame. So the order B → measure → C is not a
preference, it is a dependency.

---

## 15. The camera, corrected (2026-10-19)

**§12.3 was wrong.** It said the ROM's kernel has no working camera. It has —
both cameras work. The error was mine and it was a reading error: the config
block I quoted says

```
# CONFIG_MSM_CAMERA is not set     <- camera_v1 (the old native-API stack)
CONFIG_MSMB_CAMERA=y               <- camera_v2 (the subdev stack) IS enabled
```

and I read the first line as "no camera". The second line is the one that
matters, and it is on. `CONFIG_MSM_CAMERA_SENSOR`, `MSM_CPP`, `MSM_CCI`,
`MSM_CSI30_HEADER`, `MSM_CSIPHY`, `MSM_CSID`, `MSM_ISPIF`, `MSM_ACTUATOR`,
`MSM_EEPROM`, `MSM_EEPROM`, `MSM_OIS_CONTROLLER` are all on as well.

### 15.1 What the cameras actually are

Read off the live device (`adb shell`, root), not recalled:

```
/sys/class/video4linux/video0/name -> msm-config   (dev fd8c0000.qcom,msm-cam)
/sys/class/video4linux/video1/name -> msm-sensor   (dev 20.qcom,camera)
/sys/class/video4linux/video2/name -> msm-sensor   (dev 90.qcom,camera)
```

and the subdevice graph — 18 subdevs, which is the camera_v2 media-controller
architecture in full:

```
v4l-subdev0/1/2  msm_csiphy      v4l-subdev3..6  msm_csid
v4l-subdev7      msm_actuator    v4l-subdev8     msm_flash
v4l-subdev9      msm_eeprom      v4l-subdev10    msm_ois
v4l-subdev11     imx179          <-- REAR
v4l-subdev12     mt9m114b        <-- FRONT
v4l-subdev13/14  vfe             v4l-subdev15    msm_ispif
v4l-subdev16     msm_buf_mngr    v4l-subdev17    cpp
```

**The rear camera is a Sony IMX179 (8MP, binned to 5MP), not an OV5640.**
The front is an OmniVision MT9M114B. (The Nexus 5's rear camera being an
8MP Sony part binned down is the sort of thing that is easy to get wrong
from memory and is settled here by the device.)

**Both drivers are in our own tree**: `camera_v2/sensor/imx179.c` and
`camera_v2/sensor/mt9m114b.c`. And the whole hammerhead camera configuration
is in our tree as a device tree:

`arch/arm/boot/dts/msm8974-hammerhead/msm8974-hammerhead-camera.dtsi`
- `compatible = "qcom,imx179"`, `qcom,sensor-position = <0>` (rear),
  `qcom,cam-vreg-name = "cam_vdig"`, csiphy sd-index 0
- `compatible = "qcom,mt9m114b"`, `qcom,sensor-position = <1>` (front),
  `qcom,cam-vreg-name = "cam_vdig", "cam_vio", "cam_vana"`, csiphy sd-index 2
- `compatible = "qcom,actuator"`

So §12.5's "hammerhead's camera config is not in the tree" was also wrong.
It is in the tree, as a DT rather than a board file, because camera_v2 is
DT-based.

### 15.2 The real blocker: camera_v2 needs a userspace "imaging server"

`camera_v2/camera/camera.c` does **not** answer V4L2 requests itself. Every
one of them is forwarded to userspace:

```c
static int camera_v4l2_querycap(struct file *filep, void *fh, struct v4l2_capability *cap)
{
	camera_pack_event(filep, MSM_CAMERA_GET_PARM, MSM_CAMERA_PRIV_QUERY_CAP, -1, &event);
	rc = msm_post_event(&event, MSM_POST_EVT_TIMEOUT);
	...
}
```

and `camera_v2/msm.c`:

```c
	/* send to imaging server and wait for ACK */
	...
	if (!atomic_read(&serv_running)) {
		pr_info("%s: daemon hasn't subscribed yet!\n", __func__);
		return -EIO;
	}
```

**This is the actual architecture of the CAF camera_v2 stack: the kernel is
the mechanism, a userspace imaging server is the policy.** On Android that
server is `mm-qcamera-daemon`, and it is what holds `/dev/video0` open. Our
probe on the live device confirmed it exactly:

```
/dev/video0    open failed: Device or resource busy     <- mm-qcamera-daemon holds it
/dev/video1    QUERYCAP failed: Connection timed out    <- kernel posted an event, nobody ACKed
/dev/video2    QUERYCAP failed: Connection timed out
```

(`VIDIOC_QUERYCTRL` *did* work on the sensor nodes, because that one is
answered locally.)

The protocol is small and fully specified in the tree:
- event types: `MSM_CAMERA_NEW_SESSION`, `DEL_SESSION`, `SET_PARM`,
  `GET_PARM`, `MAPPING_CFG`, `MAPPING_SES`, `MSM_NOTIFY` (7)
- commands: `PRIV_S_CROP`, `G_CROP`, `G_FMT`, `S_FMT`, `TRY_FMT`, `METADATA`,
  `QUERY_CAP`, `STREAM_ON`, `STREAM_OFF`, `NEW_STREAM`, `DEL_STREAM`,
  `SHUTDOWN`, `STREAM_INFO_SYNC` (13)
- the reply is `MSM_CAM_V4L2_IOCTL_CMD_ACK` carrying
  `struct msm_v4l2_event_data` (16 words)

Note that the `MSM_CAM_IOCTL_*` set (70 ioctls) is **camera_v1's** and is not
implemented anywhere in camera_v2. camera_v2's userspace surface is only
those 9 `MSM_CAM_V4L2_IOCTL_*` commands plus the event protocol.

### 15.3 Why this is *not* "just port a blob"

A replacement imaging server does **not** need the vendor daemon's binaries.
It configures the graph through the **standard V4L2 subdevice API** —
`VIDIOC_SUBDEV_S_FMT`, `VIDIOC_SUBDEV_S_CROP`, `VIDIOC_SUBDEV_S_CTRL` — which
the tree implements (`include/linux/v4l2-subdev.h:126,127,138,139`). The
daemon's job is:

1. open `/dev/video0` (msm-config), subscribe to events;
2. on `S_FMT`: pick the sensor mode for the requested size, drive the sensor
   subdev, configure the ISP (`vfe`) and `cpp` subdevs, and hand the frame
   buffers to `msm_buf_mngr`;
3. on `STREAM_ON`: start the ISP, queue the buffers, ACK;
4. ACK every event.

That is a substantial but bounded piece of work, and it uses standard
interfaces — no vendor blob, no Android userspace.

**But** the ISP tuning (per-mode register settings) is *data* that on Android
comes from the HAL's camera configuration files under `/vendor/etc/camera/`.
The ROM has them. We would need the equivalents for the modes we use (QVGA
and whatever the entropy mode wants). The ROM zip is right here
(`tools/sdat2img.py` exists), so this data is obtainable.

### 15.4 The alternative: camera_v1 plus a hand-written imx179 driver

camera_v1 has **no** userspace imaging server — the kernel answers the ioctls
itself, so a capture client is just ioctls on `/dev/config0`, `/dev/control0`,
`/dev/frame0` and there is no daemon to write. That is a much smaller
userspace project.

The catch: camera_v1's sensor list (imx072, imx074, mt9d112/3, mt9e013,
mt9p012, mt9t013, ov5640, ov5647, ov7692, ov9726, s5k3e2fx, s5k4e1,
sn12m0pz, vb6801, vx6953) has **no imx179**, and camera_v1 is
platform-data based, not DT based. So this route needs:

- a new `imx179.c` for camera_v1 (modelled on `ov5640.c`, with the register
  tables lifted from camera_v2's `imx179.c`) — call it 800–1500 lines;
- a `board-8974-camera.c` supplying the platform data (sensor GPIOs, the
  `cam_vdig`/`cam_vio`/`cam_vana` regulators, clocks, I2C bus and slave
  address) — all of which we can read out of the hammerhead camera DT;
- and confidence that camera_v1's `msm_isp.c` / `msm_vfe40.c` drive this
  sensor correctly, which is untested: camera_v1 was written for the older
  MSMs, and hammerhead only ever used camera_v2.

### 15.5 Comparison

| | camera_v2 + own imaging server | camera_v1 + own imx179 driver |
|---|---|---|
| sensor driver | **already in the tree** | must be written |
| power/pin config | **already in the tree (DT)** | must be written (from the DT) |
| ISP pipeline | proven working on this device | untested for imx179 |
| userspace work | imaging server (~13 commands + subdev driving) | just ioctls |
| blob needed | no (but ISP tuning data yes) | no |
| risk | medium: the server must do what the daemon does | medium-high: unknown hardware behaviour |

The decisive advantage of camera_v2 is that **every piece of hardware
knowledge is already in the tree and already proven on this device**. The
decisive advantage of camera_v1 is that **the userspace is simple**.

The recommendation is camera_v2, because the imaging server's job is
*protocol*, and protocol is exactly the kind of thing this project has
already been doing successfully (NFC transport, display commit) — whereas
camera_v1's risk is *hardware*, and there is no way to find out without
trying.

**But this is a decision, and it is the user's.** It changes the size of the
camera work by roughly a factor of two either way.

### 15.6 Also learned: the running kernel and ours are the same generation

The kernel on the device is `3.4.113-🇺🇦-ge4e483b4ef6` — the *installed*
KernelSU kernel, gcc 16.2.0, built 2026-09-26 by `acroreiser@thinkstation`.
Our tree is also 3.4.113 CAF with the same camera_v2 code and the same
`include/linux/videodev2.h`. That is why the CAF mplane ABI
(`b.m.planes` as a pointer to `struct v4l2_plane[]`, one scalar `length`, no
`VIDIOC_S_FMT_VID_CAP_MPLANE` — multi-planar is selected by
`fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE` on the ordinary
`VIDIOC_S_FMT`) applies to both, and why the probe could be written against
it.

Also: **`adb` works on the device.** `adb devices` reports `device`, not
`offline`, because the *installed* kernel has functionfs. The "adb is
unavailable on all three kernels" finding was about the ROM zip kernel, the
TWRP kernel and the kernel inside the TWRP image — not about the kernel
that is actually flashed. The installed kernel is a fourth kernel, and it
has the adb channel working. This is a very useful debugging channel: it
is the only one we have that is not the framebuffer.

---

## 16. The imaging-server experiment (2026-10-19)

The first hands-on work on the camera. Everything below was *run*, not
reasoned. Code: `cam/` (`probe.c`, `imaging.c`, `imaging2.c`,
`imaging4.c`, `dump.c`, `dqsize.c`).

### 16.1 Method

`adb` works on the device (§15.6), so the camera could be attacked directly
rather than only through our own kernel. The vendor daemon was stopped with
`stop qcamerasvr` and restored afterwards with `start qcamerasvr` (the
device was left as found).

`/usr/include/linux/videodev2.h` on this host is byte-identical to the CAF
3.4 tree's, so the userspace programs were compiled with the *tree's* ABI,
which is the ABI our own kernel will have.

### 16.2 What the probe found on the live device

```
/dev/video0    msm-config    QUERYCAP -> ENOTTY (the config node has no querycap)
/dev/video1    msm-sensor    QUERYCAP -> ETIMEDOUT
/dev/video2    msm-sensor    QUERYCAP -> ETIMEDOUT
VIDIOC_QUERYCTRL on video1/video2 -> works, 21 controls (0x980901..0x980915)
```

The asymmetry is the whole architecture: `VIDIOC_QUERYCTRL` is answered
**locally** by the kernel, `VIDIOC_QUERYCAP` is **forwarded to userspace** and
times out because nothing is there to answer it. `/dev/video0` is held open by
`mm-qcamera-daemon` (EBUSY) — the daemon is the imaging server.

### 16.3 We can BE the imaging server

The protocol, in full, from the tree:

- open the config node `/dev/video0` (`msm_open`).
- subscribe to the private event type `MSM_CAMERA_V4L2_EVENT_TYPE`
  (`V4L2_EVENT_PRIVATE_START + 0x2000` = 0x08002000). This is what sets the
  kernel's `serv_running` flag; without it every event is refused with
  `daemon hasn't subscribed yet!`.
- the kernel posts events with `event->id` = one of
  `MSM_CAMERA_NEW_SESSION` .. `MSM_CAMERA_MSM_NOTIFY` (1..7) and blocks
  waiting for an ACK.
- ACK with `MSM_CAM_V4L2_IOCTL_CMD_ACK` = `_IOW('V', 224, struct v4l2_event)`,
  carrying the event with `u.data[]` = `struct msm_v4l2_event_data`
  (word 0 = command, word 1 = status).

Two traps, both found by experiment:

**Trap 1 — the subscription id must match exactly.** In this 3.4 tree:

```c
/* drivers/media/video/v4l2-event.c */
static struct v4l2_subscribed_event *v4l2_event_subscribed(
		struct v4l2_fh *fh, u32 type, u32 id)
{
	...
	if (sev->type == type && sev->id == id)
		return sev;
```

No "id == 0 means all" wildcard (later kernels added one). A single
subscription with id 0 matches **nothing**, and the kernel waits forever.
**We must subscribe to ids 1..7 separately.**

**Trap 2 — the ioctl number encodes the struct size, and the device's kernel
is not ours.** The running kernel is the installed `e4e483b4ef6`, built from a
*different* tree than ours, and its `struct v4l2_event` is **128 bytes** where
ours is 136. Probing every size (`dqsize.c`) showed exactly one accepted:

```
size=128  cmd=0x80805659  errno=2 (ENOENT = "no pending events")   <- correct
all other sizes: EINVAL / ENOTTY
```

So on the device, DQEVENT and CMD_ACK must be built with a 128-byte payload.
**On our own kernel, built from our tree, the 136-byte number is the right
one** — the userspace and the kernel will agree because they come from the
same header. This is only a trap when testing against the *other* kernel.

> **§16.4's conclusion was WRONG — see §18.3.** The device's ABI does *not*
> differ from ours: the two kernel trees carry byte-identical
> `include/linux/videodev2.h`, and the 128-vs-136 discrepancy was an artifact
> of our *probe*, not of the kernel. The camera ABI to program against is
> simply our tree's `struct msm_v4l2_event_data`, unchanged.

### 16.4 The result: the handshake completes

`imaging4.c`: the parent subscribes and services events; the child opens the
rear sensor node (`/dev/video1`, imx179), which makes the kernel post
NEW_SESSION and block.

```
[parent] subscribed to event ids 1..7 -- we are the imaging server
[parent] event cmd=0 status=0 session=0xc022dac0 stream=1
[parent]   ACKed
[child] open(/dev/video1): Bad address            <- was: Connection timed out
[parent] 1 events serviced
```

and the kernel's own log agrees:

```
msm_post_event: Timed out: event id is 1     <- before we ACKed
```

**The child's `open()` went from `ETIMEDOUT` (nobody answered) to `EFAULT`.**
That is the whole protocol working: the kernel posted an event, we received
it, we acknowledged it, and the kernel resumed and then failed at the *next*
step — the part the real daemon does (configuring the sensor subdev, the ISP
and the buffer manager).

### 16.5 What the device's ABI tells us, and what it does not

The raw 128-byte event we received (`dump.c`) decodes as:

```
000: 00 20 00 08   type = 0x08002000          <- MSM_CAMERA_V4L2_EVENT_TYPE
004: 00 00 00 00   u.data[0] command = 0
008: 00 00 00 00   u.data[1] status  = 0
00c: c0 da 22 c0   u.data[2]         = 0xc022dac0
010: 01 00 00 00   u.data[3]         = 1
014: 00 00 00 00   u.data[4]         = 0
018: ff ff ff ff   u.data[5]         = -1
01c: 01 00 00 00   u.data[6]         = 1
020: 01 00 00 00   u.data[7]         = 1
024: ff ff ff ff   u.data[8]         = -1
```

`camera_pack_event(filep, MSM_CAMERA_NEW_SESSION, 0, -1, &event)` sets
command = 0 and arg_value = -1. The device's kernel puts **arg_value at
u.data[8]**, ours puts it at u.data[7] — the device's
`struct msm_v4l2_event_data` has one word more before it. And its
u.data[2] is a pointer, where our tree's `session_id` is `pvdev->vdev->num`,
a small integer.

**So the device is a reference for the protocol's shape, not a drop-in
target.** The exact struct layout to program against is the one in *our* tree,
and that is what our own kernel will use.

### 16.6 What this settles, and what it does not

Settled:

- The camera is reachable from plain userspace with **no vendor blob**. We
  have already replaced the imaging server on a running device.
- The protocol is small: 7 event ids, 13 commands, one ACK ioctl.
- The kernel is the mechanism and the imaging server is the policy, and the
  policy is expressed through the **standard V4L2 subdevice API**, which the
  tree implements (`VIDIOC_SUBDEV_S_FMT` at include/linux/v4l2-subdev.h:127,
  `S_CROP` at :139).

Not settled — and this is the remaining work:

- What the ACK must *contain* for each command. `S_FMT` needs the negotiated
  format back, `QUERY_CAP` the capabilities; `STREAM_ON` needs the buffer
  manager told where the frames go.
- What the imaging server must *do* for each command: pick the sensor mode
  for the requested size and drive the sensor subdev, configure the ISP
  (`vfe`) and `cpp` subdevs, and hand the frame buffers to `msm_buf_mngr`.
- The **ISP tuning** — the per-mode register settings that on Android come
  from the HAL's camera configuration files under `/vendor/etc/camera/`. The
  ROM has them; `tools/sdat2img.py` exists to get at them. This is data, not
  code, and it is version-specific to the ROM's camera HAL.

### 16.7 The order of work from here

1. **On our own kernel**, with the tree's ABI: enable the camera options
   (§14.5 Phase A) and build. The `MSM_CAMERA` / `MSMB_CAMERA` / `IMX179` /
   `MT9M114B` option set is now known exactly, from the live config.
2. Port `imaging4.c` to the tree's ABI and run it as our own init's first
   stage: subscribe, ACK, and see the sensor node open.
3. Work the command list one at a time, starting with `QUERY_CAP` and
   `S_FMT`, using the framebuffer as the observable.
4. Then `STREAM_ON` + buffers, and a frame on the panel.

This is the "capture client" work, and it is now known to be a bounded
protocol problem rather than a blob-porting problem.

---

## 17. 2026-10-19: AT_HWCAP settled, camera enabled, adb image prepared

### 17.1 AT_HWCAP — the soft-float question, answered on the device

Measured with `getauxval(AT_HWCAP)` on the running device:

```
AT_HWCAP = 0xfb8d7
  HWCAP_FP    (bit 11) : YES
  HWCAP_ASIMD (bit 12) : YES
```

The running kernel builds `CONFIG_VFP=y`, `CONFIG_VFPv3=y`, `CONFIG_NEON=y`,
`CONFIG_KERNEL_MODE_NEON=y` — **the kernel itself uses VFP/NEON**, so the FPU
demonstrably works.

**The soft-float mandate is not load-bearing.** It may be treated as settled:
a *hosted* MicroPython is permitted, not only the freestanding `minimal` port.

Where the distinction actually bites:

| binary | ABI | VFP insns | why |
|---|---|---|---|
| our payload (`rootfs/init`) | **soft-float** | **0** | this is what becomes the device; the mandate is honoured where it matters |
| the throwaway probes (`cam/*.c`) | hard-float | 273 | built with the distro `arm-linux-gnueabihf-gcc` because that is the only ARM toolchain on the host with a libc |

The hard-float probes work because: they are statically linked, so there is no
dynamic loader and the ABI tag only affects how glibc passes float arguments
to itself; the FPU is present; and ARM Linux enables the coprocessor per
process on first use. The one thing that would break them is an FPU-less
kernel, which this is not.

### 17.2 A duplicate kernel tree was living in RAM

`/tmp/opencode/kern/android_kernel_lge_hammerhead` — 704 MB in tmpfs, git
`9ac11678`, **the same commit as the vendored tree**. All our builds have
always used `vendor/android_kernel_lge_hammerhead` (the `--tree`
argument), so the copy was pure waste. Removed; tmpfs went from 1.9 GB to
1.2 GB used. `vendor/k_quirc` (the QR decoder) was moved out of
tmpfs into the project, where it belongs.

### 17.3 The camera option set is now applied and verified

`tools/profiles/hammerhead-camera.list` states the camera configuration as
intent, with `CONFIG_X=y` / `CONFIG_X=n` lines. `apply-defconfig.py` gained a
`--disable` flag, symmetric with `--enable`, so "we want this sensor off
because we do not have it" sits next to "we want that sensor on".

The values are the running kernel's own, read from `/proc/config.gz`, and
**every symbol in them exists in our tree** — checked one by one:
MSM_CSI30_HEADER, MSM_CSI22_HEADER, OIS_CONTROLLER, OIS_ROHM_BU24205GWL,
OIS_ONSEMI_LC898111A, MSMB_JPEG, MSM_VIDC_V4L2, IMX179, MT9M114B, MSM_CPP,
MSM_CCI, MSM_CSIPHY, MSM_CSID, MSM_ISPIF, MSM_ISPIF_V1, MSM_EEPROM,
MSM_CAMERA_SENSOR, MSMB_CAMERA. Our tree is the same CAF generation as the
ROM's kernel, so the configuration transfers verbatim.

Two things that are *not* separate options and are worth knowing:

- **There is no `CONFIG_MSM_ACTUATOR` in camera_v2.** The autofocus driver is
  `camera_v2/sensor/actuator/msm_actuator.c`; it matches the `qcom,actuator`
  DT compatible and is built by `camera_v2/sensor/Makefile` as part of
  `MSMB_CAMERA`. The live kernel's config has no `MSM_ACTUATOR` either, and
  the live device still has an `msm_actuator` subdev — which is how we know.
  (Adding it to the profile was an error; the tool caught it by refusing to
  enable a symbol that does not exist.)
- `MSM_ION` is the wrong name; the options are `CONFIG_ION=y` and
  `CONFIG_ION_MSM=y`, and both were already present.

### 17.4 devtmpfs, and why the init mounts it itself

The camera's char-device major is allocated at runtime
(`alloc_chrdev_region`), so `/dev/video0..2` **cannot** be baked into the
initramfs cpio the way `fb0`, `event1` and `bcm2079x` are.
`CONFIG_DEVTMPFS=y` is now enabled for that reason (and for `/dev/ffs.ep0`).

`CONFIG_DEVTMPFS_MOUNT` is deliberately **not** set: in 3.4 the automount is
in `prepare_namespace()`, and `kernel_init()` runs `run_init_process()` — our
init — *before* that. For an initramfs the automount would arrive after we
had already started. So the init mounts devtmpfs on `/dev` itself, with a
fallback: if the mount fails, or succeeds but `/dev/graphics/fb0` is not
there, it **unmounts and carries on with the baked cpio nodes** — the
configuration that has already proved the screen, the touch and the NFC.

`/dev/graphics/fb0` is unchanged by devtmpfs: the MSM framebuffer driver
uses class `graphics`, so devtmpfs creates the node at the same path.

### 17.5 The adb image: our kernel + the ROM's ramdisk

`rootfs/stock/unpacked/` holds the stock boot image unpacked: a
kernel and a ramdisk. The ramdisk is the **standard Android boot ramdisk** —
`init`, `fstab.hammerhead`, `default.prop`, `metadata` and empty `dev`,
`proc`, `sys`, `mnt` directories. Everything else (including `adbd`) comes
from `/system`, which that `init` mounts.

So **our kernel + that ramdisk = the whole Android userspace running on our
kernel**, with adb. That is the debugging channel this project has been
missing, and it needs nothing but the functionfs we already enabled.

The cmdline is reused verbatim from the stock header (`bootimg.py pack`
refuses to invent header fields):
`console=ttyHSL0,115200,n8 androidboot.hardware=hammerhead user_debug=31
msm_watchdog_v2.enable=0 androidboot.bootdevice=msm_sdcc.1 loop.max_part=7`

The risk to watch: Android's `init` will start radio and modem services that
our stripped kernel deliberately cannot support. If that turns into a reboot
loop rather than a degraded boot, the fallback is TWRP's ramdisk instead,
which is far less demanding.

---

## 18. Corrections: the kernel tree, and the adb path

Three things were wrong. All three are recorded here because §15 and §16
still contain the wrong versions.

### 18.1 The kernel tree is acroreiser's, not z3DD3r's

We had been building from
`https://github.com/z3DD3r/android_kernel_lge_hammerhead.git` @ `9ac11678`
(2023-06-05). The kernel the device actually runs — the installed one, and
the ROM's — is built from
**`https://github.com/acroreiser/android_kernel_lge_hammerhead`** @
`466f069d` (2026-10-10). Same defconfig names, same camera_v2 layout, same
option names at the same line numbers, but a much closer match to the live
config.

Measured, after switching:

| | z3DD3r tree | acroreiser tree |
|---|---|---|
| options in live but not in ours | 170 | **111** |
| value-level differences from the live config | ~200 | **152** |

and every remaining difference is accounted for: the radio symbols we removed
(`BCMDHD`, `BCM4339`, `BRIDGE_EBT_*`, `WLAN`, `BT`, …), plus a handful of
Kconfig symbols that exist in one tree's inventory and not the other's.

**The camera block and the USB gadget block now match the live, working
kernel exactly** — same options, same values.

The two patches we had (`f_fs.c` uidgid include, `camera_v1/ov5640.c`
includes) belonged to the other tree and are not needed here: `f_fs.c` is
only built with `CONFIG_USB_FUNCTIONFS`, which is off, and `camera_v1` is off.
Both patch files were deleted rather than left to rot.

### 18.2 adb is `USB_G_ANDROID`, not `USB_FUNCTIONFS`

This was the expensive mistake. The standing note in this project said
"`CONFIG_USB_FUNCTIONFS` is off in all three existing kernels, so adbd finds
no `/dev/ffs.ep0` and sits at `offline`" — and the fix was duly made: we
enabled functionfs.

**That was wrong.** `CONFIG_USB_FUNCTIONFS` is the functionfs *filesystem*
(`/dev/ffs.ep0`), used by the generic `g_ffs` gadget function. It is not the
adb path. The adb path is **`CONFIG_USB_G_ANDROID`** — the AOSP "android"
USB gadget — whose adb function is implemented *on top of* f_fs internally
(`drivers/usb/gadget/android.c` refers to ffs 66 times) and which creates
`/dev/usb-ffs/adb/ep0` itself.

The proof is the config of the kernels that are known to give a working adb:

```
TWRP kernel (bbba49d1)      CONFIG_USB_G_ANDROID=y   CONFIG_USB_FUNCTIONFS is not set
installed kernel (e4e483b4)  CONFIG_USB_G_ANDROID=y   CONFIG_USB_FUNCTIONFS is not set
ROM zip kernel               CONFIG_USB_G_ANDROID=y
```

and TWRP's own adbd:

```
strings sbin/adbd | grep usb-ffs
  /dev/usb-ffs/adb/ep0
  /dev/usb-ffs/adb/ep1
  /dev/usb-ffs/adb/ep2
strings sbin/adbd | grep usb_init
  [ usb_init - using FunctionFS ]
```

So the correct config is `USB_G_ANDROID=y` and functionfs **off**.

And enabling functionfs is not a harmless alternative — it **clears**
`USB_G_ANDROID`, because the two are siblings in the "USB Gadget Drivers"
choice (`drivers/usb/gadget/Kconfig`). That is exactly what we built, and
the result was: **no USB device enumerated on the host at all**. Not "adb
offline" — no USB device whatsoever.

`tools/profiles/hammerhead-keep.list` now says `CONFIG_USB_G_ANDROID=y` and
`CONFIG_USB_FUNCTIONFS=n`, with the reasoning recorded in the file.

### 18.3 The 128-vs-136 `v4l2_event` was our probe's fault, not the kernel's

§16.3 concluded that "the device's kernel is not ours" because the running
kernel wanted a 128-byte `struct v4l2_event` and our tree's header says 136.

**Both kernel trees carry byte-identical `include/linux/videodev2.h`** — I
diffed them. So the difference was in the *userspace* struct our probe
compiled, not in the kernel. `struct v4l2_event` contains a
`struct timespec`, and the kernel's `__kernel_timespec` and glibc's
`struct timespec` are not the same size on 32-bit ARM — the kernel's uses a
64-bit `tv_sec`, glibc's does not by default. The probe therefore sent an
ioctl number encoding the wrong size, and the kernel rejected it with EINVAL.

The lesson is the one this project keeps re-learning: **measure the ABI on
the device you are talking to, and build the probe against the same header
the kernel was built with.** Our `cam/` programs are built against
the tree's header precisely so that this cannot happen for our own kernel.

The camera ABI to program against is simply our tree's
`struct msm_v4l2_event_data` — 16 words, `arg_value` at word 7 — unchanged.
The device's is one word longer because the device's kernel is from a
different tree; that difference goes away when we build and run our own.

### 18.4 Where the camera work stands, restated

- The camera is enabled in our kernel, with the option set copied from the
  live working kernel and verified.
- The **protocol** is proven: we subscribed to the private event type, the
  kernel set `serv_running`, it posted NEW_SESSION, we received it, we
  ACKed it, and the blocked `open()` of the sensor node went from
  `ETIMEDOUT` to `EFAULT` — the handshake completed and the kernel resumed
  into the work the real daemon does.
- We do **not** yet have frames. What remains is (a) what the ACK must
  contain for each command, and (b) the imaging server's own work: pick the
  sensor mode for the requested size and drive the sensor subdev, configure
  the ISP (`vfe`) and `cpp` subdevs, and hand the frame buffers to
  `msm_buf_mngr`.
- The next step is to run that on **our** kernel, with adb, where the ABI is
  ours and the config is the live one.

### 18.5 `f_fs.c` needs one include — in BOTH trees

`drivers/usb/gadget/f_fs.c` uses `kuid_t`, `kgid_t`, `make_kuid`, `make_kgid`,
`uid_valid` and `gid_valid`, but never includes `<linux/uidgid.h>`. The tree
*has* that header (`include/linux/uidgid.h`, 3541 bytes) and `linux/fs.h` does
not pull it in, so the file does not compile.

This was invisible until now, because `f_fs.c` was only ever built as part of
`CONFIG_USB_FUNCTIONFS` — which was off in every kernel. With
`CONFIG_USB_G_ANDROID=y` it **is** built, because:

```c
/* drivers/usb/gadget/android.c */
#include "f_fs.c"
```

The android gadget compiles the functionfs *filesystem* into itself. That is
also why it has its own `ffs_function_init`, `ffs_configs` and
`ffs_aliases[256]`, and why `/sys/class/android_usb/android0/f_ffs/aliases`
exists — so `write /sys/class/android_usb/android0/f_ffs/aliases adb` works
without the generic `g_ffs` gadget function.

The same one-line patch is needed in the acroreiser tree as in the z3DD3r one
(`patches/0001-usb-gadget-f_fs-include-uidgid.patch`):

```c
 #include <linux/export.h>
 #include <linux/hid.h>
+#include <linux/uidgid.h>
 #include <linux/workqueue.h>
```

### 18.6 The TWRP USB init, and what it needs from the kernel

`init.recovery.usb.rc` in the ramdisk, which is the whole contract:

```
on post-fs
    mkdir /dev/usb-ffs 0770 shell shell
    mkdir /dev/usb-ffs/adb 0770 shell shell
    mount functionfs adb /dev/usb-ffs/adb uid=2000,gid=2000
    write /sys/class/android_usb/android0/f_ffs/aliases adb

on property:sys.usb.config=adb
    write /sys/class/android_usb/android0/enable 0
    write /sys/class/android_usb/android0/idVendor 18D1
    write /sys/class/android_usb/android0/idProduct 4EE2
    write /sys/class/android_usb/android0/functions adb
    write /sys/class/android_usb/android0/enable 1
    start adbd
    setprop sys.usb.state ${sys.usb.config}
```

Every one of those paths is under `/sys/class/android_usb/`, which **only
exists when `CONFIG_USB_G_ANDROID=y`**. With functionfs instead, that class
directory is not there, every write fails, the gadget is never enabled, and
the host sees no USB device at all. That is precisely what we observed.

`default.prop` carries `persist.sys.usb.config=adb`, and the last rule
(`on property:persist.sys.usb.config=*` → `setprop sys.usb.config ...`) is
what turns that into the trigger for the `sys.usb.config=adb` block above.
