# Security audit: logs, prints and storage (updated 2026-11-13)

**Scope.** Everything the delivered image runs: the boot ramdisk (init
stub, TWRP), the kernel, `camd`, the MicroPython Unix port and the Krux
Python package.  The question is not "is the code careful?" but **what
bytes leave the process, where do they go, how long do they live, and
who can read them.**  A debugging aid that is harmless on a development
bench is a leak on a signing device.

This document is the map and the plan.  The "Immediate" items are
already in the tree or are the first changes to make; the rest is
ordered by what it protects.

---

## 1. Assets

| # | Asset | Where it lives normally | Sensitivity |
|---|---|---|---|
| A1 | mnemonic words / seed / extended key | Python heap, then framebuffer | **critical** |
| A2 | passphrase | Python heap, keyboard/QR UI | **critical** |
| A3 | xprv / signed PSBT / signatures | heap, QR/display | **critical** |
| A4 | SeedKeeper PIN and secrets | heap, NFC/APDU | **critical** |
| A5 | entropy source frames | camera pipeline, heap | **critical** (the seed is derived) |
| A6 | QR/screen frames (they *show* A1–A5) | camera, framebuffer, display buffers | **critical** |
| A7 | wallet fingerprint / xpub | heap, display, logs (fingerprint only) | medium |
| A8 | settings (theme, timeouts, network) | `/data`/flash file | low |
| A9 | device serial, kernel addresses, debug state | kernel, logs | low, but helps exploitation |

The deliverable has **no asset that may be persisted** except a backup
the user explicitly asks for — and that backup is blocked in the
bundled image.  Everything else must be RAM-only and must not be
readable after the process ends.

---

## 2. Threat model

| T | Attacker | Capability |
|---|---|---|
| T1 | person with the device + USB | fastboot, adb in TWRP (root), can boot another image |
| T2 | Android booted afterwards | root/privileged processes, reads `/data`, the framebuffer, `/proc`, dmesg |
| T3 | UART/serial | reads the console; SysRq if enabled |
| T4 | local process on our firmware | connects to the world-writable sockets, reads shared memory |
| T5 | cold-boot / RAM remanence | reads reserved memory (framebuffer, ION, carveout) after a warm reboot |

T2 and T5 are why *RAM-only* is not automatically safe: the
framebuffer, the camera ION buffers and the kernel log are reserved
memory that a later boot can read, and the vendor kernel does not zero
pages on allocation (`INIT_ON_ALLOC`/`INIT_ON_FREE` do not exist here).

---

## 3. The map

### 3.1 Kernel

| Sink | What | Lives | Read by | Note |
|---|---|---|---|---|
| `dmesg` ring (`CONFIG_PRINTK`) | boot, camera/ISP/CPP per-frame lines, USB, input | RAM, ring until reboot | any process (`dmesg_restrict=0`), T2/T3 | **no pixels**, but addresses/config; the vendor prints flood it |
| serial console | the same on `ttyHSL0` | wire | T3 | cmdline has `console=ttyHSL0,115200` + `uart_console=enable` |
| `CONFIG_RAMOOPS=y` | would persist dmesg across reboot | reserved RAM | T3/T5 | **no `CONFIG_PSTORE`**, no dts node mounted (`/sys/fs/pstore` absent) — built but inert; remove anyway |
| `CONFIG_DEBUG_FS=y` | kernel debug interface | RAM | root, T1/T2 (mountable) | not mounted by us |
| `CONFIG_KALLSYMS=y` | symbol addresses | kernel | root | helps exploitation |
| `CONFIG_MAGIC_SYSRQ=y` | SysRq over the serial console | — | T3 | can crash/dump |
| `CONFIG_IKCONFIG=y` | `/proc/config.gz` | RAM | any | not secret, but free reconnaissance |
| `/dev/mem`, `/dev/kmem` | physical RAM | — | — | absent (`DEVKMEM` off; no node), `STRICT_DEVMEM` off is moot while absent |
| `CONFIG_ELF_CORE=y` | core dumps of crashed processes | file | T2 | no `core_pattern` restriction; a crash may dump the Python heap (A1) |
| `CONFIG_ANDROID_PERSISTENT_RAM=y` | 1 MB reserved region, marker at `0x100F0000` | reserved RAM | T5 | the wipe marker is magic/seq/CRC only — no secret; the region is needed for the power-off wipe |
| `INIT_ON_ALLOC/FREE` | page zeroing | — | — | **absent**; freed pages keep A1 until reused |

Kernel debug prints themselves do not carry the secret, but they carry
the state needed to exploit the device and they are readable by T2/T3.
The camera/ISP/CPP per-frame lines are the bulk of the ring.

### 3.2 Boot ramdisk / init stub

| Sink | What | Lives | Read by | Note |
|---|---|---|---|---|
| `/data/krux/fw.log` | the firmware's stdout/stderr: every `print()` in the Python, including camera AE, page errors, "QR detected" | **persistent /data**, 0644 | T1/T2 | development build only; the release stub logs to `/dev/null` |
| `/data/krux/camd.log` | camd's ~194 print sites: frame indices, exposure, AF scores, ISP geometry, crop rects, dump paths | **persistent /data**, 0644 | T1/T2 | development build only; the release camd has no trace |
| `say()` | stub messages | recovery console | T3 | not secret |
| adb/USB | the sealed image renames `adbd` to `adbd.off`; no adb listener until `/sbin/start-adb` | — | T1 | adb is on demand; `/sbin/start-serial` is the non-USB way in |
| `/data` mount | userdata, auto-mounted for the firmware and camd | — | T1/T2 | the delivered image still mounts and writes it |
| fb band paint (`rcS`) | 400 rows, buffers 0/1 | reserved RAM | T5 | not a clear; the rest of the previous screen survives |

`KRUX_DELIVERY` is set for the **firmware** only, and only inside
`start_the_firmware`; `camd` starts earlier and inherits no delivery
mode, so it cannot currently choose its storage policy.

### 3.3 `camd`

| Sink | What | Lives | Read by | Note |
|---|---|---|---|---|
| `/data/krux/scan.raw` | the **published preview** (960×1280 luma) | **persistent /data** | T1/T2 | `dump_frames()`, triggered by `dump_now` (ctl `D`, `sensor.dump()`) or `CAMD_DUMP_RAW` |
| `/data/krux/scan_full.raw` | the **full sensor window** (up to 960×1280/3280×2464) | **persistent /data** | T1/T2 | same |
| `/data/local/tmp/frame*.nv12` | raw NV12 frames | **persistent /data** | T1/T2 | gated by `debug_frames`/`CAMD_DEBUG_FRAMES`; default off, one env var on |
| `/tmp/krux/frame` | live preview, 1.2 MB | tmpfs, 0600 | root, T4 (if root) | the same bytes the page shows |
| `/tmp/krux/crop` | QR retry crop, 1.6 MB | tmpfs, 0600 | root | A6 |
| `/data/krux/state/*` | tuning knobs (stream, mode, af, focus, profile) | **persistent /data** | T1/T2 | not secret, but persistent configuration |
| `/data/krux/cppmsg.bin`, `ispcfg` | camera configuration | persistent | T1/T2 | not secret |
| `camd.sock` | frame protocol: `A` keep-alive, `F` frame copy, `R` focus, `D` dump, `E` exposure, `W` register write, `C` crop, `V` VTS | socket, **0666** | **any local process** | `D` can force the frame dumps; `W` can write sensor registers |
| `camd-ctl.sock` | the same controls for a host client | socket, **0666** | any local process | `D` dumps frames; `E`/`R`/`c` |
| stdout → `camd.log` | the print stream | persistent | T1/T2 | 0644 |

The frame sockets are the largest live exposure: a local process can ask
for the preview (A6) or trigger the persistent dumps; the permissions
make that possible for any UID on the device.

### 3.4 MicroPython port

| Sink | What | Lives | Read by | Note |
|---|---|---|---|---|
| `lcd.save_ppm(path)` | **the entire framebuffer** as PPM | any path the caller chooses | — | still exported; `scripts/display/*` use `/data/local/tmp/fb.ppm`; a page or a future script can write the seed screen |
| `lcd.flush`/`preview_flip` | the framebuffer contents | reserved RAM, triple-buffered | T5/T2 | the panel and the scanout buffers keep the last frame |
| `image` stats/save | the camera image | heap | — | no file write found in the stats path |
| port `print`s | startup/diagnostics | fw.log | T1/T2 | via the firmware's stdout |

### 3.5 Krux Python

| Sink | What | Lives | Read by | Note |
|---|---|---|---|---|
| `/data/krux/last-qr.rgb565` | the **last QR frame** (RGB565) when a scan ends incomplete | **persistent /data** | T1/T2 | `qr_capture.py` on every cancelled/failed scan; the frame can contain A1–A5 |
| `print()` in `camera`/pages | AE values, errors, "QR detected", part counts | fw.log | T1/T2 | no payload or words printed |
| `/flash/seeds.json` (`MNEMONICS_FILE`) | **encrypted mnemonics** backup | `/data/krux/flash` (dev) / `/opt/krux/flash` (bundled) | T1/T2 | the encrypted store; in bundled it is in the initramfs tmpfs (lost on reboot) but the UI does not say so |
| `/flash/settings.json` | settings | persistent | T1/T2 | no secret |
| SD `seeds.json` | the same on a card | card | physical | user's own card |
| SeedKeeper | PIN and secrets over APDU | heap | — | no print/trace found in the port's `seedkeeper.py` |
| QR result | the decoded payload | heap only | — | the `print` is only "QR detected" (the payload is commented out) |

### 3.6 Persistent and reserved places (the "after a reboot" list)

| Place | Survives | Contains | Access |
|---|---|---|---|
| `/data/krux/*.raw`, `last-qr.rgb565` | power cycle | **A5/A6** | T1/T2 |
| `/data/krux/*.log` | power cycle | traces, no pixels | T1/T2 |
| `/data/krux/flash/seeds.json` | power cycle | encrypted A1 | T1/T2 |
| `/data` in general | power cycle | whatever a previous dev image wrote | T2 |
| framebuffer / scanout (reserved RAM) | **warm reboot** | the last screen (A1/A6) | T5/T2 (the display device) |
| ION/CPP camera buffers (reserved RAM) | **warm reboot** | the last camera frames (A5/A6) | T5/T2 |
| kernel log ring (RAM) | warm reboot until reuse | traces, addresses | T2/T3 |
| ramoops (reserved) | power cycle | nothing today (no pstore backend), but built | T5 |
| panel (physical) | until redrawn | the last screen | anyone looking; the bootloader/TWRP redraw |

---

## 4. Findings, by severity

**S1 — QR frame persisted on every failed scan.**
`qr_capture.py` writes `last_img` to `/data/krux/last-qr.rgb565` whenever
the scan ends incomplete.  The user cancels or misses often, and the
frame can hold a seed, xprv or PSBT shown on another screen.  This is a
plain file on userdata.

**S1 — camera frames dumped to persistent storage on request.**
`dump_frames()` writes the full preview and sensor window to
`/data/krux/scan.raw` / `scan_full.raw`; `debug_frames` writes
`/data/local/tmp/frame*.nv12`.  Any local process can trigger the dump
through the 0666 control socket (`D`), and the firmware exposes
`sensor.dump()`/`camera.dump()`.

**S1 — the live frame sockets are world-writable.**  `camd.sock` and
`camd-ctl.sock` are `0666`: any process on the device can read the
preview (A6) or force a dump.

**S1 — the framebuffer/panel/reserved RAM is a durable store.**
The display shows A1/A6 while it is on; the reserved framebuffer and
the ION buffers survive a warm reboot and are readable from a later
Android/TWRP boot.  Nothing clears them on page exit or power-off, and
`lcd.save_ppm()` can write the whole screen anywhere.

**S2 — adb/TWRP is the first thing that comes up.**  The stub waits for
adb; the standard image boots TWRP with root adb.  An attacker can read
`/data` (S1 files, logs, `seeds.json`), the framebuffer, dmesg, and
`/proc`.  The firmware's own root shell is not needed for this.

**S2 — kernel debug surface.**  `dmesg_restrict=0`, `DEBUG_FS=y`,
`KALLSYMS=y`, `MAGIC_SYSRQ=y`, `RAMOOPS=y`, `ELF_CORE=y`, no
`INIT_ON_ALLOC`, and the vendor camera prints flood the ring.  The
serial console is enabled in the cmdline.

**S2 — persistent logs.**  `fw.log` and `camd.log` are on `/data`,
0644, and truncated only at the next start.  They contain camera usage,
errors and paths (not pixels or words), but they are a trace of the
device's use and survive a power cycle.

**S2 — the delivered image still mounts `/data`.**  The bundled
firmware runs from the boot ramdisk, but the stub mounts userdata and
`camd` writes its state/logs/dumps there.

**S3 — the encrypted seed backup is not clearly blocked.**  In the
bundled image `/flash` resolves into the initramfs (tmpfs), so a backup
is silently lost on power-off; in development it is a real file on
`/data`.  The policy says backups are blocked in bundled; the UI should
say so instead of writing into a tmpfs.

**S3 — core dumps.**  A MicroPython crash can write a core of the
process heap (A1) wherever `core_pattern` points.

**S4 — prints.**  The AE/error prints are not secrets, but they are the
only reason the firmware writes a log at all.

---

### 4.1 What the audit found on the development device (2026-11-12)

Not hypotheticals — these were present on the device:

| Found | Size / mode | Meaning |
|---|---|---|
| `/data/krux/last-qr.rgb565` | 2.4 MB, 0600 | the **last QR frame** from an incomplete scan (the scan.raw write in `qr_capture.py`) |
| `/data/local/tmp/frame0.nv12`, `frame2.nv12`, `frame3.nv12` | ~8 MB each, **0666** | raw camera frames from the `debug_frames` path, world-readable |
| `/data/krux/frame.raw` | 1.2 MB, 0644 | a camera frame from the AE work |
| `/data/krux/camd.sock`, `camd-ctl.sock` | **0666** | any local process could read the preview or force a dump |
| `/data/krux` | **0777** | the deployed tree and logs, world-writable |
| `fw.log`, `camd.log` | persistent | the boot/camera traces |

All the picture files were removed, the failed-scan write is gone, the
sockets are `0600`, and the dumps require `CAMD_ALLOW_DUMPS`.  The
directories were left as they are on this development copy; a delivered
boot should create them `0700` (or not at all).

## 5. What is *not* a leak (verified)

- The QR payload is not printed: only `QR detected` / part counts (the
  payload prints are commented out).
- The mnemonic words are not printed or logged anywhere in the port.
- `camd` prints counts, geometry, exposure and scores — not pixel
  values and not the QR contents.
- The kernel does not log pixel data; its camera lines are frame
  counts/addresses.
- The SeedKeeper page has no APDU/PIN trace in the port.
- The `/tmp/krux` preview/crop are mode 0600 and on tmpfs (RAM, gone on
  the next boot); they are a live T4 exposure, not a persistent one.

---

## 6. The plan

### 6.0 How the profile is applied (the mechanism)

It is a **build variant of the same tree**, not a branch and not a
separate source tree.  The **default is the release profile**:
`KRUX_RELEASE=1` and `KRUX_SEAL=1` are assumed by the build scripts.
Development and the audit are explicit opt-outs, so forgetting a flag
produces the secure image, never a debug one.

```sh
# the shipping image (release + sealed; no adb)
scripts/build/build-release.sh

# the audit image: release, adb kept for the measurement
KRUX_SEAL=0 scripts/build/build-release.sh

# development: logs, dumps, save_ppm, adb (the existing dev flow)
KRUX_RELEASE=0 scripts/deploy-firmware.sh
KRUX_RELEASE=0 rootfs/build-stub-ramdisk.sh --bundle
```

`KRUX_RELEASE=0` is the one a developer disables while working; it
implies the debug capabilities and adb.  `KRUX_SEAL=0` is the one we
disable only for the final audit.  A plain `build-release.sh` with no
environment is the sealed, shipping image.

The Python side follows the same rule: the stub exports
`KRUX_RELEASE=1` by default and `0` in development, and `debug()` is a
no-op unless the environment says development — a missing variable
means release.

| Component | Mechanism | What the flag changes | Artifact |
|---|---|---|---|
| kernel | a `krux-release.config` fragment applied by hand after the defconfig (manual `.config` + `olddefconfig`; the profile format cannot express `=y`) | the crash-persisted channels in Phase 3 (no ram console, no RAMOOPS, ELF_CORE off), while keeping the persistent-RAM reservation for the wipe marker | a separate kernel build dir and `zImage-dtb` |
| init stub | compile-time `-DKRUX_RELEASE` in `build-stub-ramdisk.sh --release` | log fds to `/dev/null`, no adb wait, userdata policy, `setenv("KRUX_DELIVERY")`, `RLIMIT_CORE=0`, framebuffer clear | a separate stub binary in the release ramdisk |
| camd | compile-time `-DKRUX_RELEASE` in `build-camd.sh` | a `CAM_LOG` macro that is a no-op, the dump functions and `CAMD_DUMP_RAW` compiled out, sockets 0600, state dir chosen from the delivery env | a separate camd binary in the bundle |
| MicroPython port | compile-time `-DKRUX_RELEASE` in `build-micropython.sh` | `lcd.save_ppm` out of the module table, the port diagnostics out | a separate `micropython-arm32-release` |
| Krux Python | runtime, because the package ships as source: a `debug()` helper that is a no-op unless the development delivery, and the backup gated by `delivery` | the prints, the backup page, the last-qr path (already removed) | the same package, selected by the delivery env |
| build | a thin `scripts/build/build-release.sh` that calls the four builds with the release defaults and packs the image; `KRUX_SEAL=0` keeps adb for the audit, `KRUX_RELEASE=0` is the development build | nothing new in the source; the orchestration and the artifact names (`*-release`, `*-release-audit`) | `boot-standard-release.img` (default), `*-release-audit.img` (`KRUX_SEAL=0`) |
| verification | `scripts/security-check.sh` | the gate: grep the image for the paths/functions, boot it, use the pages, assert no files/logs/old framebuffer | pass/fail |

Two things this deliberately is **not**:

- **Not a runtime switch for the compiled parts.**  A flag an
  attacker can flip is not a control; the dumps and `save_ppm` are
  compiled out.  Only the Python is runtime-gated, because it ships as
  source; even there the dangerous write is removed rather than
  conditional.
- **Not a second tree or branch.**  The camera/AE work continues on the
  development build; the release build is the same commits with the
  profile on.  The delivery modes (development / standard) and the
  security profile are orthogonal: a bundled image can be debug (for
  testing) or release (delivered).

### Phase 1 — release build profile (removes the S1 file writes)

One switch, `KRUX_RELEASE`, used by all three components:

- **camd**: compile out `dump_frames()`, the `debug_frames` and
  `CAMD_DUMP_RAW` paths, and the per-frame print statements (or send
  them to a small RAM ring with no file).  Keep only a one-line start
  and fatal errors.
- **sockets**: `camd.sock` and `camd-ctl.sock` become `0600` (the
  firmware is root; the dev host connects through adb as root, so the
  development flow still works).
- **camd storage**: `CAMD_PERSIST_DIR` becomes `/tmp/krux` when
  `KRUX_DELIVERY=bundled` (no `/data` write); the state is then
  volatile and rebuilt from defaults each boot.
- **init stub**: when bundled, `FIRMWARE_LOG` and `CAMD_LOG` are
  `/dev/null`; set `KRUX_DELIVERY` **before** `start_camd` so the
  daemon knows; do not mount userdata for the bundled image (or mount
  it read-only and only to wipe `/data/krux` on request).
- **Krux**: remove the `last-qr.rgb565` write; make every `print` go
  through a `debug()` that is a no-op in a release build; block the
  backup page in bundled with an explicit message (not a silent tmpfs
  write).
- **MicroPython port**: compile `lcd.save_ppm` out of the release
  binary (keep it in development).

### Phase 2 — boot and access (done)

The delivered mode is the **standard** one: the device comes up in TWRP
with adb off, and the firmware is started by holding volume-up or by
`/sbin/start-krux`.  It does not autostart the firmware.  This changed
from the original plan (which autostarted the firmware); the standard
mode is better for the seal because a device in the field looks like a
normal TWRP until the operator asks for Krux.

- The release firmware runs from the read-only `/opt/krux`; `/data` is
  mounted for settings only, and the release writes no logs there.
- The sockets and shared files are `0600`; the release has no trace or
  frame dumps.
- adb and the USB gadget are removed in Phase 6, after the final audit;
  the sealed image keeps them off until `/sbin/start-adb`.

### Phase 3 — kernel: the crash-persisted channels only

An unlocked bootloader makes runtime hardening pointless — anyone can
boot their own kernel.  The only kernel concern is what **this kernel
leaves for the next one to read**:

| Channel | Config | Action |
|---|---|---|
| `/proc/last_kmsg` (the previous kernel's log) | `CONFIG_ANDROID_RAM_CONSOLE=y` | **off** |
| the reserved region behind it | `CONFIG_ANDROID_PERSISTENT_RAM=y` | **keep on** — the power-off wipe marker lives at `0x100F0000` in this region; with the ram console off the region is still reserved and nothing logs into it |
| pstore/ramoops | `CONFIG_RAMOOPS=y`, `CONFIG_PSTORE` off | **RAMOOPS off** |
| core files of a crashed process | `CONFIG_ELF_CORE=y` | **off** (and the stub's `RLIMIT_CORE=0`) |

The fragment is `scripts/build/krux-release.config`; apply it to the
kernel build's `.config`, `olddefconfig`, rebuild `zImage-dtb`, and pair
that kernel with the release ramdisk.  **This has not been applied to the
shipping image yet**: the delivered kernel still has the ram console,
ramoops and `ELF_CORE` enabled (PSTORE is off).  The wipe marker does not
depend on the ram console, only on the reserved region, so the fragment
keeps `CONFIG_ANDROID_PERSISTENT_RAM=y`.

Nothing else in the kernel needs to change: dmesg/debugfs/kallsyms/SysRq/
serial are runtime and the device is unlocked anyway.

### Phase 4 — RAM and framebuffer hygiene (T5)

This is a belt, not a `/proc` channel: the next kernel would have to map
the reserved memory itself (a custom kernel or a cold-boot attack), and
the bootloader is unlocked anyway.  The one high-value case is the
**screen at power-off**, because the auto-shutdown can fire while a
mnemonic or seed page is displayed.

**The display (what it takes: ~50 lines, low risk).**

- `fb0` is 1080×1920, 32 bpp, stride 4352, triple-buffered; buffer 2 is
  only partially backed (the `rcS` note).  The panel is video-mode: the
  MDSS copies the framebuffer into the scanout buffer on
  `MSMFB_DISPLAY_COMMIT`, so the scanout memory holds the *last
  committed* screen.
- Add `lcd.scrub()`: zero the mapped framebuffer (both full buffers and
  the partial one), commit the black frame, blank the panel
  (`FBIOBLANK`/the blank attribute), and leave the backlight off.  The
  scanout copy then holds black too.
- Call it from the idle/power-off path (`auto_shutdown`, before
  `power_manager.shutdown()`), and before the screen saver releases the
  display.  This is the case that matters: the 10-minute timer can fire
  on the mnemonic page.
- Test: in development, write a known pattern, scrub, and read the
  framebuffer back (`lcd.save_ppm` is available there); the reserved
  scanout can only be checked from the next boot.

**The camera (what it takes: a scrub command, moderate risk).**

- `camd` holds 8 ION buffers (Y/UV), two bounce buffers, and the
  `/tmp/krux/frame` + `crop` regions.  The ION buffers are the live
  pipeline: the ISP writes them continuously, so they cannot be zeroed
  while the stream runs without stopping it (the fragile path).
- When no camera page is open the buffers hold the room, not the
  secret: the camera pages feed the auto-shutdown, so a power-off while
  the QR/entropy view is up does not happen.  The display scrub above
  covers the page that can be up at power-off (the mnemonic).
- So the useful, cheap part is: zero `/tmp/krux/frame` and `crop` when
  the camera page closes, and zero the bounce buffers.  The ION zeroing
  is only worth it if the reserved-memory threat is in scope; it needs a
  `Z` command that pauses the sensor, zeroes the buffers and resumes,
  and it exercises the same teardown that has been fragile.

**Core dumps.**  The stub sets `RLIMIT_CORE=0` for the firmware and
camd; the kernel fragment also turns `CONFIG_ELF_CORE` off.  The kernel
has no `INIT_ON_ALLOC`, so freed heap pages are not zeroed; the app
cannot fix that, which is why the display is scrubbed instead.

### Phase 5 — verification (a release gate)

A `scripts/security-check.sh` that:

1. greps the built image and the binaries for `last-qr`, `scan.raw`,
   `scan_full.raw`, `frame%d.nv12`, `save_ppm`, `CAMD_DUMP_RAW`,
   `debug_frames`, and fails if any is reachable in release;
2. boots the delivered image, uses the QR/entropy/SeedKeeper pages, then
   checks: no files under `/data/krux` (or `/data` not mounted), empty
   logs, the sockets 0600;
3. checks `dmesg_restrict`, the absence of `/sys/kernel/debug`,
   `/sys/fs/pstore`, `/dev/mem`, and the console;
4. after a warm reboot, checks that the framebuffer no longer holds the
   previous screen (a known pattern written before the reboot must not
   be readable).
5. `scripts/misc/check-airgap.py` checks the kernel `.config` and the
   ramdisk: no WLAN/`CFG80211`/`BCMDHD`, no BT/RFCOMM/BNEP/HIDP, no MSS
   baseband/RMNET/QMI-WWAN, no FM tuner, and no
   `wpa_supplicant`/`hostapd`/`wificond` or Broadcom Wi-Fi/Bluetooth
   firmware.  `CONFIG_NFC_BCM2079X=y` is the one allowed wireless
   interface.  It runs from `build-release.sh`.

The check must run on the **delivered** image, not the development one.

### Phase 6 — the seal: adb and the USB gadget **last** (done)

The seal was deferred until the UART gave a way back in; `start-serial`
now does that, so the seal is in the delivered image.  The image the
audit runs on remains the release build **with adb**
(`KRUX_SEAL=0`); the sealed image is the default build from the same
commit and differs only by that flag.

The implementation is stronger than the original plan:

- the stub renames `/sbin/adbd` to `/sbin/adbd.off` in **every** path,
  not only the firmware path, so TWRP's init cannot restart it;
- the `krux-firmware` service is `disabled`, so the firmware does not
  start at boot beside TWRP;
- `/sbin/start-adb` restores `adbd` and enables adb on demand, and
  `/sbin/start-serial` gives a root shell on `ttyHSL0` (115200 8N1)
  even when adb is off or broken;
- the release build ignores `/data/krux/state/autostart`.

Procedure (completed):

1. Built `KRUX_SEAL=0` (release, adb on); ran the Phase 5 check and the
   on-device audit.
2. Built the default (`KRUX_SEAL=1`) from the same commit; the
   difference is the stub/ramdisk seal, not the kernel, port, `camd` or
   Python.
3. Smoke-tested the sealed image: TWRP boot, QR/entropy, SeedKeeper,
   settings, the screen-saver/power-off, and the RAM wipe.
4. Recorded the sealed artifact: `artifacts/built/boot-final-sealed.img`,
   sha256 `807facd8a5b521daf675b29e910efb441b77e432873f327030c079c540130892`.

adb is the only way to inspect the final bytes on the device, which is
why the seal is still the last step; `start-serial` is the fallback if a
sealed device has to be inspected or recovered.

---

## 7. Immediate items (first changes)

1. **Done.** The `last-qr.rgb565` write is removed (`qr_capture.py`).
2. **Done.** The camd sockets are `0600`, and the frame dumps require
   `CAMD_ALLOW_DUMPS` (the bench sets it; the delivered image does not).
3. **Done.** The release stub logs to `/dev/null`, does not mount
   userdata, keeps `/flash` and `/sd` in the initramfs, exports
   `KRUX_RELEASE`, and disables core dumps; the release camd keeps its
   state in `/tmp/krux`.
4. **Done.** `lcd.save_ppm` is compiled out of the release port; the
   encrypted flash backup is refused in the bundled image.
5. **Open.** Apply the release kernel fragment (Phase 3) in a manual
   `.config`/`olddefconfig` build.  The shipping kernel still has the
   ram console, ramoops and `ELF_CORE` on.  Keep
   `CONFIG_ANDROID_PERSISTENT_RAM=y`: the wipe marker lives in that
   region.
6. **Partly done.** The kernel-only power-off wipe zeroes free RAM
   (1488 MB) before userspace; it is verified.  The framebuffer/scanout
   and the ION/camera carveouts are reserved memory and are **not**
   covered by that pass; the display scrub and the buffer zeroing on
   page exit remain open (Phase 4).

### Status at 2026-11-13

The release profile is built and the **audit image** (`KRUX_SEAL=0`,
adb kept) has been run on the device:

- no `fw.log` or `camd.log` is created; no `/data/krux/state`;
- the shared preview and the QR crop are `0600` under a `0700`
  `/tmp/krux`, and the camera sockets are `0600` in a `0700`
  `/data/krux`;
- the release port has no `save_ppm` (the development port does);
- `scripts/security-check.sh` passes on the built image.

Audit image: `artifacts/built/boot-standard-release-audit.img`,
sha256 `680a7bee6abc69e019381e4b372122ccd61ecbb4fdaa63ab5bc7f9517d5e605b`
(the 2026-11-12 build).

The **sealed** image is built, flashed to the recovery partition and
verified on the device:

```
artifacts/built/boot-final-sealed.img
20,064,256 bytes
sha256 807facd8a5b521daf675b29e910efb441b77e432873f327030c079c540130892
kernel 673242ee6e09e25edc658dfb6ee05e794eb821e5e35e08f2cd647a3588652805
```

That includes the seal, `start-adb`, `start-serial`, `/sbin/recovery`,
`/system/bin/sh`, and the kernel-only RAM wipe on power-off.  The two
open items above are the release kernel fragment (ram console/ramoops/
core files) and reserved-memory/framebuffer scrubbing.

The audit image is the bundled delivery, so its auto-shutdown powers
the device off after the configured timeout (10 minutes by default).
For an extended audit set *Settings → Security → Auto-shutdown → Never*;
the setting lives in the initramfs `/flash` and is gone on the next
boot, which is the intended behaviour for the release.

Items 1–2 are tiny and safe; 3 and 4 are build changes; 5–6 need the
kernel/ramdisk image and a cold boot.  The rest of the plan is ordered
by the phase above.
