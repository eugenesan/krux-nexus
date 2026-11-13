# Security audit: logs, prints and storage (2026-11-12)

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
| `INIT_ON_ALLOC/FREE` | page zeroing | — | — | **absent**; freed pages keep A1 until reused |

Kernel debug prints themselves do not carry the secret, but they carry
the state needed to exploit the device and they are readable by T2/T3.
The camera/ISP/CPP per-frame lines are the bulk of the ring.

### 3.2 Boot ramdisk / init stub

| Sink | What | Lives | Read by | Note |
|---|---|---|---|---|
| `/data/krux/fw.log` | the firmware's stdout/stderr: every `print()` in the Python, including camera AE, page errors, "QR detected" | **persistent /data**, 0644 | T1/T2 | created/truncated by the stub |
| `/data/krux/camd.log` | camd's ~194 print sites: frame indices, exposure, AF scores, ISP geometry, crop rects, dump paths | **persistent /data**, 0644 | T1/T2 | no pixels, but a full trace of what the camera did |
| `say()` | stub messages | recovery console | T3 | not secret |
| adb/USB | adbd comes up **first**; the stub waits `USB_WAIT_S` for it | — | T1 | the standard image boots TWRP with root adb |
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
separate source tree.  One flag, `KRUX_RELEASE=1`, flows through the
existing build scripts; the development build is unchanged (no flag).

| Component | Mechanism | What the flag changes | Artifact |
|---|---|---|---|
| kernel | a `krux-release.config` fragment applied after the defconfig (the pattern `build-romcfg.sh` already uses) + a release `EXTRA_CMDLINE` | the options in Phase 3 (no RAMOOPS/DEBUG_FS/SYSRQ, dmesg_restrict, ELF_CORE off, vendor prints), the cmdline (no console/uart) | a separate kernel build dir and `zImage-dtb` |
| init stub | compile-time `-DKRUX_RELEASE` in `build-stub-ramdisk.sh --release` | log fds to `/dev/null`, no adb wait, userdata policy, `setenv("KRUX_DELIVERY")`, `RLIMIT_CORE=0`, framebuffer clear | a separate stub binary in the release ramdisk |
| camd | compile-time `-DKRUX_RELEASE` in `build-camd.sh` | a `CAM_LOG` macro that is a no-op, the dump functions and `CAMD_DUMP_RAW` compiled out, sockets 0600, state dir chosen from the delivery env | a separate camd binary in the bundle |
| MicroPython port | compile-time `-DKRUX_RELEASE` in `build-micropython.sh` | `lcd.save_ppm` out of the module table, the port diagnostics out | a separate `micropython-arm32-release` |
| Krux Python | runtime, because the package ships as source: a `debug()` helper that is a no-op unless the development delivery, and the backup gated by `delivery` | the prints, the backup page, the last-qr path (already removed) | the same package, selected by the delivery env |
| build | a thin `scripts/build/build-release.sh` that calls the four builds with `KRUX_RELEASE=1`, picks the kernel, packs the image, and runs the check; `KRUX_SEAL=1` is the final adb/USB-off variant | nothing new in the source; the orchestration and the artifact names (`*-release`, `*-release-audit`) | `boot-standard-release.img` (`*-release-audit.img` for the audited, adb-on build) |
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

### Phase 2 — boot and access (everything except adb)

- The delivered image **autostarts the firmware**; TWRP is available
  only by holding the volume key, and only in the development image.
- Mount userdata **read-only** in the delivered image, or not at all;
  the firmware/camd use the initramfs and `/tmp`.
- Wipe `/data/krux` (and any old `scan.raw`, `last-qr`, logs) on the
  first delivered boot; the standard mode's wipe already does this —
  make it part of the delivered boot, not only the explicit wipe.
- Set the socket/shm/file modes `0600`; the logs `0600` if any remain.

**adb and the USB gadget are deliberately not removed here.**  They are
the audit channel; they are removed only in Phase 6, after the final
batch has been audited on the device.

### Phase 3 — kernel and cmdline

Release config (the build `.config` is `/tmp/opencode/ko/.config`):

| Option | Now | Release |
|---|---|---|
| `CONFIG_PSTORE*` | off | off |
| `CONFIG_RAMOOPS` | **y** | **n** |
| `CONFIG_DEBUG_FS` | **y** | **n** (or not mountable) |
| `CONFIG_KALLSYMS` | **y** | n, or `kptr_restrict=2` |
| `CONFIG_MAGIC_SYSRQ` | **y** | **n** |
| `CONFIG_SECURITY_DMESG_RESTRICT` | **off** | **y** and `dmesg_restrict=1` |
| `CONFIG_ELF_CORE` | **y** | n; `core_pattern=/dev/null`, `ulimit -c 0` |
| `CONFIG_IKCONFIG` | y | y is fine (no secret) |
| `STRICT_DEVMEM` | off | on (defence in depth; no `/dev/mem` today) |
| vendor camera prints | per-frame | `pr_debug`/removed; `loglevel=1` |
| cmdline | `console=ttyHSL0`, `uart_console=enable`, `selinux=permissive` | no console/uart in the delivered image; permissive only where the recovery needs it |

The serial console is the UART attack (T3): disable it in the release
cmdline.  The extreme delivery mode can keep a UART build.

### Phase 4 — RAM and framebuffer hygiene (T5)

- **Clear the framebuffer and all three scanout buffers** at start and
  before power-off; blank the panel.  The `rcS` band paint is not a
  clear, and a power-off currently leaves the last screen in reserved
  RAM.
- **Zero the preview/QR shared regions and the ION/CPP buffers** when a
  camera page closes and before power-off.
- The kernel has no `INIT_ON_ALLOC`; the app cannot zero freed heap
  pages, so the remaining defence is: do not leave the secret in a
  reserved buffer, and clear the display path explicitly.
- Disable core dumps in the stub (`setrlimit(RLIMIT_CORE, 0)`).

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

The check must run on the **delivered** image, not the development one.

### Phase 6 — the seal: adb and the USB gadget **last**

The image the audit runs on is the release build **with adb**.  The
sealed image is built from the same commit and differs only by
`KRUX_SEAL=1`:

- the USB gadget is not enabled and the stub does not wait for adb;
- TWRP's adbd is not started (the stub stops the recovery before it
  comes up, or the adbd service is disabled);
- nothing else changes: the kernel, the port, `camd` and the Python are
  byte-identical to the audited build.

Procedure:

1. Build `KRUX_RELEASE=1` (adb on); run the Phase 5 check and the whole
   audit on the device; record the artifact hashes and the result.
2. Build `KRUX_RELEASE=1 KRUX_SEAL=1`; verify that the difference from
   the audited artifact is limited to the seal (the stub/ramdisk; the
   other components' hashes unchanged) and that the audited build
   passed.
3. Smoke-test the sealed image without adb: boot, QR, entropy,
   SeedKeeper, settings, the screen-saver/power-off.  The audit itself
   is not repeated on the sealed build; it was done on the identical
   build minus the seal.
4. Record the sealed artifact's sha256 (and the audited one) in
   [cam-good-state.md](cam-good-state.md) / the release notes; the seal
   commit is the shipping tag.

This is why the seal is last: adb is the only way to inspect the final
bytes on the device, and removing it changes nothing the audit is
checking.  Removing it first would remove the instrument.

---

## 7. Immediate items (first changes)

1. **Done.** The `last-qr.rgb565` write is removed (`qr_capture.py`).
2. **Done.** The camd sockets are `0600`, and the frame dumps require
   `CAMD_ALLOW_DUMPS` (the bench sets it; the delivered image does not).
3. Move the camd's state to `/tmp/krux` and send the logs to
   `/dev/null` when `KRUX_DELIVERY=bundled`; set the env before
   `start_camd` (S2).
4. Compile out `lcd.save_ppm` in the release build (S1 capability).
5. Add the release kernel config/cmdline profile (Phase 3) — the next
   image build.
6. Clear/zero the framebuffer and the shared/ION regions on page exit
   and power-off (Phase 4).

Items 1–2 are tiny and safe; 3 and 4 are build changes; 5–6 need the
kernel/ramdisk image and a cold boot.  The rest of the plan is ordered
by the phase above.
