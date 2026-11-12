# Packaging plan, delivery modes, and the two Krux trees

Status: plan of record, 2026-11-11.  Supersedes the packaging sections of
`docs/build-guide.md` (which still describes XZ as the default and a removed
`KEXEC=` wipe path).

## 1. The three delivery modes

| | mode | firmware lives | camd/MP start | `/data/krux` holds |
|---|---|---|---|---|
| a | development (current) | `/data/krux`, or bundled in the ramdisk | automatically at boot | everything, including the deployment |
| b | standard | bundled in the ramdisk, read-only | **not** at boot: boot trigger or a script | config only |
| c | extreme | a minimal initramfs userspace, no partitions mounted | automatically | nothing (no persistence) |

### (a) — what exists today

`rootfs/build-stub-ramdisk.sh` builds a TWRP ramdisk with `sbin/recovery`
renamed to `sbin/recovery.stock`, `sbin/krux-init` (from `rootfs/init-stub.c`)
installed as the `krux` service, and `init.recovery.service.rc` rewritten to
run it.  `rootfs/make-boot-image.sh` packs that into a copy of the TWRP
template.

`rootfs/init-stub.c` then: waits 8 s for USB/adb, mounts `/data`, watches
the volume-up key for 4 s (`wait_for_the_key`, `init-stub.c:105-137`), and
either execs TWRP (`start_twrp`, :140-146) or starts the firmware
(`start_the_firmware`, :255-352) — which starts `camd` (:183-253) and execs
`micropython boot.py` (:347).  Firmware directory choice: `/data/krux` if it
holds `micropython` + `boot.py`, else the bundled `/opt/krux` (:305-321).

This is mode (a) and it works.

### (b) — the work

**(b)(I) XZ ramdisk compression.**  Two different unpackers are involved and
this is the whole subtlety:

* `fastboot boot <image>` — the **kernel** unpacks the initramfs.  The
  current defconfig has `CONFIG_RD_XZ=y`
  (`linux/arch/arm/configs/lineageos_hammerhead_defconfig:135`), so XZ works
  on that path.  It did *not* work in the earlier capture kernels: the
  camera config in `artifacts/built/cam-yonly/kernel.config` has
  `CONFIG_RD_XZ` unset, which is why commit `d060148` recorded "an XZ
  ramdisk is accepted by `fastboot boot` but boots nothing".
* An image **flashed to the boot partition** — the **bootloader** unpacks
  it, and this bootloader only looks for a gzip ramdisk.  An XZ ramdisk
  there boots nothing, silently.

So: XZ is safe for the `fastboot boot` delivery this project actually uses
(nothing is ever flashed), and *unsafe* for a distributable flashed image.
Decision: keep **gzip** as the default for images that may be flashed, add
`COMPRESS=xz` as an explicit opt-in for the `fastboot boot` path, and
document the constraint at the top of `build-stub-ramdisk.sh` (it already
carries a comment saying the opposite of the above — it says the *bootloader*
decides; that part is right, but the conclusion "that is why this is gzip"
should also mention that the kernel path would accept XZ).

**(b)(II) A fully working TWRP.**  The stub replaces TWRP's own init
(`sbin/recovery` → `sbin/recovery.stock`) and its service file.  The
"full TWRP" check is: boot the *stock* `artifacts/twrp-3.7.0_9-HH.R.17-MADV_WIPEONFORK.img`
and confirm recovery works (adb shell, the TWRP terminal, `twrp` CLI, and
that it does not fall into our firmware).  Then boot our image and confirm
the same recovery is reachable via the volume-up key.  Everything TWRP does
must keep working; the only thing we take from it is the ramdisk.

**(b)(III) Bundling.**  `build-stub-ramdisk.sh --bundle` already installs
`/opt/krux` (micropython, camd, `boot.py`, `krux/`, `embit/`, `flash/`) into
the ramdisk.  Two things are missing for mode (b):

1. the stub must **prefer** `/opt/krux` over `/data/krux` (today `/data/krux`
   wins, :305-321) — in mode (b) `/data/krux` must be config-only;
2. the firmware and `camd` must **not** start automatically.

**(b)(IV) The wipe.**  Today "Erase user's data" (`krux/src/krux/pages/flash_tools.py:184-202`)
walks SPIFFS and calls `flash.erase()`, which on this port removes exactly
two files — `flash/settings.json` and `flash/seeds.json`
(`krux/src/flash/__init__.py:139-147`), resolved against the firmware's CWD
(`/data/krux`).  It does **not** remove `/data/krux`.

The requirement is that the wipe removes `/data/krux`.  In mode (a) that
would delete the firmware itself (micropython, `boot.py`, `krux/`), which is
why the requirement only makes sense once (b)(III) exists: with the
firmware bundled read-only in the ramdisk, `/data/krux` is purely state and
can be wiped wholesale.  Implementation: the port's `flash.erase()` grows a
"remove the deployment" step that deletes the *contents* of the firmware
directory (`/data/krux`, minus the running interpreter's own directory
entry) and then reboots.  In mode (a) the wipe must refuse, or degrade to
"settings and seeds only", rather than brick the device.

### (c) — deferred, but plan it now

A standalone minimal userspace: a stub init that starts `camd` + MicroPython
and nothing else — no partition mounting, no `/data`, no persistence.  It
needs UART for any diagnosis (the current device has no working UART, which
is why it is deferred).  The pieces already exist: `rootfs/init-stub.c` is
the stub, `rootfs/build-stub-ramdisk.sh` can build a ramdisk without TWRP,
and `rootfs/init-*.c` contain older variants (`init-freestanding.c`,
`init-airgap.c`) that explored the same ground.  What is missing is the
*minimal* ramdisk (no TWRP, no adb), a way to get logs out (UART or a
one-shot dump to `/data` before the mount is dropped), and a camera path
that does not need `/data` (the daemon currently writes its log and socket
there).

## 2. The two Krux trees

Facts (verified, 2026-11-11):

* `krux-nexus/krux` — branch `nexus-port`, 143 commits past the common
  ancestor `9c53a1ed`, **not pushed** (the upstream remote tip is 23
  commits behind).
* `krux-mobile/krux` — branch `seedkeeper-secure-channel`, 7 commits past
  the same ancestor, **not pushed**.
* The common ancestor `9c53a1ed` ("seedkeeper: correct the AID comment")
  is present in both; both carry the same `unified-sighash*` branch SHAs.
  The two clones even share byte-identical pack files.
* `src/krux` divergence: 89 common paths, **55 identical, 34 different**,
  2 only in the nexus tree (`base32_compat.py`, `_base32.py`), 0 only in
  the mobile tree.  2,839 differing lines.
* The biggest differences are **device work, not shared work**:
  `camera.py` (711 lines — the hammerhead auto-exposure controller),
  `touch.py` (374), `qr_capture.py` (346), `buttons.py` (231),
  `auto_shutdown.py` (177), `display.py` (34), `wdt.py`, `light.py`.
* The SeedKeeper work is the opposite: `seedkeeper.py` (181),
  `seedkeeper_sc.py` (116), `pages/seedkeeper.py` (78), `login.py` (55),
  `settings_page.py` (28).
* The Android app does **not** use `src/boot.py`.  It carries a *copy* of
  the mobile tree's `src/krux` (21 files patched) with the patch set
  recorded in `kruxmobileapp/tools/krux-delta/*.patch`, and injects the
  device layer as mocks (`mocks/board_mock.py`, `mocks/lcd_mock.py`,
  `mocks/sensor.py`, `mocks/nfc.py`).

### 2.1 Recommendations

**Do not unify the two branches.  Unify the shared *library* instead.**

The two trees are not two versions of one thing; they are two *products*
that happen to share a body of domain logic:

| | nexus-port | seedkeeper-secure-channel |
|---|---|---|
| runtime | MicroPython (unix port, arm32) | CPython on Android (Kivy + buildozer) |
| device layer | in-tree shims (`src/board.py`, `src/nfc.py`, `src/sensor.py`, `src/flash/`) + C modules (`ports/unix/*.c`) | injected mocks (`kruxmobileapp/mocks/*`) + pyjnius providers |
| NFC | BCM2079x NCI over I2C (`src/nfc.py`) | Android `NfcAdapter` over pyjnius |
| camera | `camd` daemon over a unix socket | CameraX `ImageAnalysis` |
| display | `/dev/graphics/fb0` via `lcd_bcm.c` | Kivy widget |

Merging those two runtime models into one branch would produce a tree where
every device file is `#ifdef`-ed by platform — exactly the mess the port
avoided by having a separate tree.  The duplicated 2,839 lines, however,
are mostly *not* device code: they are the same pages, the same SeedKeeper
APDU layer, the same translations, seen from two platforms.

So the recommendation is a three-layer structure, with the shared layer
becoming its own upstream-able package:

```
krux-core/                 (new, runtime-agnostic)
  src/krux/core/
    seedkeeper.py          APDU + card policy          (portable)
    seedkeeper_sc.py       secure channel (pure-python SHA-1/HMAC)
    bip39.py psbt.py key.py sighash.py wallet.py kef.py
    pages/                 login, mnemonic_loader, home, tools, ...
    translations/
  board.py lcd sensor nfc  -> abstract interfaces + the port's implementations
krux-nexus/krux            branch nexus-port        (hammerhead port)
krux-mobile/krux           branch seedkeeper-secure-channel (android lib)
```

Rules that make this work, each derived from the actual divergence:

1. **The core must be valid on MicroPython *and* CPython 3.**  Concretely:
   no `hashlib`/`uhashlib` in shared code.  The mobile branch's pure-Python
   SHA-1/HMAC in `seedkeeper_sc.py` is exactly the right pattern and the
   nexus tree's `hmac`/`uhashlib_hw.py` version is exactly the wrong one to
   standardise on.  Adopt the pure-Python implementation in the core and let
   the nexus port keep its hardware SHA-1 only as an optimisation.
2. **The core owns the protocol, the port owns the transport.**  The
   SeedKeeper APDU layer, the secure-channel state machine and the card
   policy (`should_offer()` vs `available()`) belong in the core; the
   `IsoDepTransport` implementation belongs to the port (nexus:
   `src/nfc.py` over BCM2079x; android: pyjnius).  The mobile branch's
   `no_reader_reason()` / `unavailable_reason()` split is a port concern
   that the core should express as a small capability object.
3. **Keep the camera, display, touch and power layers out of the core.**
   `camera.py` (711 differing lines), `touch.py` (374), `auto_shutdown.py`
   (177) are port-specific by nature: the nexus port drives `camd` and a
   real VCM/AE controller; the Android app drives CameraX.  What *is*
   portable is the page-level logic (`qr_capture.py`, `capture_entropy.py`)
   behind the existing sensor interface — but even `qr_capture.py` now
   differs by 346 lines because the nexus port added the AE/AF control
   loops, so that is the first candidate to re-abstract when the two are
   next synced.
4. **Fix the divergence mechanism before the code.**  The Android app
   consumes a *copy* of `src/krux` and carries a 21-file patch set.  As
   long as that is the integration mechanism, every core change needs a
   regenerated patch and the two trees drift.  The fix: move the copy to a
   build-time step that applies a *generated* patch (it already is one —
   `tools/krux-delta/*.patch`), and shrink it to near zero by moving the
   shared code into the core.
5. **Do not merge history.**  Both branches are already in the same upstream
   repo shape with a common ancestor; merging them would create a monster
   merge that touches 34 files for no functional gain, and the Android app
   would then have to be patched against the merged tree instead of the
   small one.

### 2.2 What (d) means for the branch set

Keep three long-lived branches, and add a fourth that is the *product* of
(2.1) and (e):

| branch | in | purpose |
|---|---|---|
| `master` | upstream `selfcustody/krux` | the K210 reference |
| `nexus-port` | `krux-nexus/krux` | the hammerhead port (device layer + camera) |
| `seedkeeper-secure-channel` | `krux-mobile/krux` | the SeedKeeper work, Android-shaped |
| `seedkeeper-secure-channel-nexus` | `krux-nexus/krux` | **the same SeedKeeper work applied to the hammerhead** — this is (e) |

All four live in one upstream repo.  The fourth branch is the "clone of
`seedkeeper-secure-channel`" that (e) asks for, and it is also the vehicle
for the core extraction: land the core on the mobile branch first, then
port the core to the nexus port.

## 3. (e) — plan and apply

Goal: give the SeedKeeper work its own hammerhead packaging, without
unifying the branches.

1. **Branch**: create `seedkeeper-secure-channel-nexus` from `nexus-port` in
   `krux-nexus/krux`.
2. **Port** the mobile branch's SeedKeeper-relevant changes onto it, in
   dependency order:
   * `src/krux/seedkeeper.py` — the secure-channel traces, the public
     `login_again()`, the card offer policy (`should_offer`),
     `no_reader_reason()`, the host-entropy cascade in
     `create_new_mnemonic`.
   * `src/krux/seedkeeper_sc.py` — with the **pure-Python** SHA-1/HMAC (the
     nexus tree's `hmac`/`uhashlib_hw.py` path is not portable, but the
     hammerhead does have hardware SHA-1; keep the pure version as the
     reference and optimise later).
   * `src/krux/pages/seedkeeper.py` — the card menu, the reader prompt
     wording, the "no reader" explanations.
   * `src/krux/pages/login.py`, `pages/home_pages/mnemonic_backup.py`,
     `pages/settings_page.py` — the menu wiring (this is also where task
     2(a) lands).
   * `i18n/translations/*.json` — the strings the nexus tree is missing
     (the nexus JSONs have no `"Via SeedKeeper Card"` key, no
     `"Tap your SeedKeeper card\nflat on the back,\nover the camera.\nHold it
     still."`, no `"Cancelled"`, `"OK"`, `"No card"`, and they still carry
     the stale `"Tap your SeedKeeper card"` key that `i18n/i18n.py:113`
     flags).
   * `tests/` — the mobile branch's three extra test files
     (`seedkeeper_sc_helper.py`, `test_seedkeeper_sc.py`,
     `test_seedkeeper_trace.py`) adapted to the hammerhead's mocks.
3. **Port the device layer**: the nexus tree needs `src/nfc.py` to expose
   what the ported code expects (`transport()`, `should_offer()`,
   `no_reader_reason()`); today it exposes `transport()` only.
4. **Package** the branch with mode (a): the same
   `build-stub-ramdisk.sh --bundle` + `make-boot-image.sh` pipeline, with
   the branch's own `krux/` tree bundled into `/opt/krux`, and the same
   `deploy-firmware.sh` (which pushes the tree's `krux/`).
5. **Verify**: the SeedKeeper menu items appear and the card path works on
   the hammerhead (the NFC hardware is there: `nfc/` in this repo is the
   BCM2079x HAL).

## 4. Task 2 — the rest of the list

**(a) SeedKeeper menu redistribution** (both trees, and on the (e) branch):

1. "Load Seed from Card" moves from the main screen into the *load
   mnemonic* menu (`pages/mnemonic_loader.py`, next to "Via Camera", "Via
   Manual Input", "From Storage").
2. "Save Seed to Card" moves into the *backup mnemonic* menu
   (`pages/home_pages/mnemonic_backup.py`, next to "QR Code", "Encrypted",
   "Other Formats").
3. The main-screen "SeedKeeper Card" item moves to the *Tools* menu
   (`pages/tools.py`).
4. The wording is aligned with the neighbouring entries: "Via SeedKeeper
   Card" (as the loader's submenu already uses), "Backup to SeedKeeper
   Card" or "Save to SeedKeeper Card" — to be decided with the rest of the
   menu's vocabulary.

**(b) Hardware bring-up documentation** — a single `docs/hw-bringup.md`
recording the methods that actually worked on this device: probing the
vendor HAL by adding printk to the kernel's ioctl handlers, booting the
vendor stack on our kernel, `fastboot boot` (never flash), the ION/vb2
buffer accounting, the evdev event injection for touch/keys, the daemon's
control socket, the AE/AF loop, and the capture corpus method.  The point
is that a future device does not have to rediscover them.

**(c) Cleanup / archive** — the context problem.  Today `docs/` is 31,854
lines, of which two machine-written logs are 16,805 lines (60 % of the
bytes): `project-log.md` (12,944) and `compaction-log.md` (3,861).  The
plan: archive the logs and the superseded camera briefs, make
`docs/milestone-2026-11-10-snapshot2.md` the single canonical camera
document, give `docs/README.md` real links and a short "where to start"
section, and fix the already-broken links (`README.md:11` points at a
deleted file).

## 5. The standard mode, as built and tested (2026-11-12)

The standard delivery mode works on the device.  What it took:

**The init stub (`rootfs/init-stub.c`)**

* `standard_image()` -- the mode the image was built for
  (`build-stub-init.sh --standard`, passed on by
  `build-stub-ramdisk.sh --standard`).
* `autostart_the_firmware()` -- whether the firmware starts at boot.
  The standard image says no; a file `/data/krux/state/autostart` with
  a 1 there says yes, so one image can be tried both ways.  The
  *copy* of the firmware follows the image, not this flag: a standard
  image always runs the read-only copy in the ramdisk.
* The volume up key means different things in the two modes: in the
  development image it is TWRP (as it always was), in the standard
  image it is the firmware, and the default is TWRP.  The key is also
  read as a *state* (`EVIOCGKEY`), not only as an event: a key held
  from power-on produces no press event, and the stub would have
  missed it.
* `krux-init start` -- a client that starts the firmware on a running
  image.  It stops the "krux" service (which is TWRP by then) and
  starts the firmware.  `/sbin/start-krux` is the script that calls
  it; it hands the work to a new session (`setsid`), because stopping
  the service ends TWRP and with it the shell that runs the script
  when that shell is TWRP's own terminal.

**Two bugs that cost an afternoon**

* `system()` cannot be used in this ramdisk: there is **no `/bin/sh`**,
  only `/sbin/sh`.  Every `system()` call failed silently -- which
  meant the stub had *never* stopped TWRP.  The stub now forks and
  execs the program itself (`run_program`).
* Android's `setprop ctl.stop <service>` is **two** arguments: the
  property is `ctl.stop` and the value is the service's name.  Passing
  "ctl.stop krux" as one argument makes toybox refuse the call.

**What was measured**

| | result |
|---|---|
| the standard image | boots into TWRP 3.7.0 (the menu draws, `/data` is mounted, adb works) |
| the volume up trigger | starts the bundled firmware; TWRP never comes up |
| `/sbin/start-krux` | stops TWRP and starts the bundled firmware, from adb or from TWRP's terminal |
| the bundled firmware | runs from the read-only `/opt/krux`; the data partition holds the settings only |
| XZ ramdisk | 8.7 MB against 14.5 MB for gzip; it boots and works on the `fastboot boot` path |
| the wipe | removes `/data/krux` (36 entries); the device then comes up with the same firmware and no state |

**Two things to know about the display**

* The framebuffer is **double buffered by panning**: the port's `lcd`
  module draws into the buffer at `pan_offset` and pans the display to
  it.  A framebuffer dump therefore shows *two* pictures, and the one
  in the first 1920 rows may be stale -- after TWRP has run, that is
  exactly what happens: TWRP's last frame is still in the first
  buffer while the firmware draws in the second.  The panel shows the
  firmware; the dump does not.
* The panel handover works.  It looked broken until the second buffer
  was read.

## 6. Execution order

1. `docs/packaging-plan.md` (this document).
2. Task 2(c) — cleanup and the entry point.  Highest value per minute:
   it is what makes the rest of the work cheap to pick up.
3. Task 2(a) — the SeedKeeper menu redistribution.
4. (b) — the packaging work: the stub's modes, the start script, the XZ
   test, the bundle preference, the TWRP check, the wipe.
5. (e) — the `seedkeeper-secure-channel-nexus` branch and its packaging.
6. Task 2(b) — the bring-up document.
7. (c) — the minimal userspace, when UART is available.
