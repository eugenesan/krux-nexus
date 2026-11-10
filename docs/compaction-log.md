# Compaction log

Every compaction of this session is recorded here, one section per
compaction, newest last.  A compaction replaces the conversation with a
summary; that summary is the only thing that survives it, and it lives
in the session database.  This file is the on-disk copy, so the record
of what was dropped (and what was kept) survives the session.

Written by `.opencode/plugins/compaction-log.ts`.

## 2026-10-21 00:23:28 UTC - session compaction (manual)

* session: `ses_ec9cfaf7efferMLQZnvjWxHDO8`

## Objective
- Air-gapped Nexus 5 (hammerhead): own kernel + minimal Linux userspace running the **Krux** firmware in MicroPython, with a working camera and a working SeedKeeper NFC transport.
- Two interleaved threads: **camera (main thread)** — real frames; **MicroPython/Krux (background)** — screen, touch, and a runtime that replaces the TWRP app.
- New side tasks: (a) a **compaction log** written to `compaction.log.md` on every compaction; (b) **relocate needed assets out of `/tmp/opencode` (tmpfs) into the project dir** to survive a reboot, and delete stale assets while leaving kernel/compiled object data in `/tmp/opencode`.

## Requirements
- Air-gapped: no modem/Wi-Fi/BT, no privileged/signature permissions, revertible by reflash.
- **Device access protocol**: one device, two threads, interleaved — camera needs fastboot/TWRP+our kernel; MicroPython work runs on the ROM's Android. Camera boot+run cycle is atomic; MicroPython work happens between cycles.
- **No serial console** → adb is the only debugging channel.
- **No own init**: reuse the TWRP ramdisk; full rootfs replacement deferred until a serial adapter exists.
- **Minimize changes to the Krux project** so changes can be merged upstream; prefer the MaixPy API surface over firmware edits.
- Runtime design: hide our firmware "under" TWRP — the **firmware is the default payload**, **holding Vol+ gives TWRP**; adb must start before our stub.
- Update the session log after each conclusion/report or change (log = `airgap-standalone.log.md`).
- Use `\7` (bell) for the user's attention for physical actions; keep command timeouts short; TWRP boots in ~20 s; avoid `sed 's/^/  /'` (use `awk '{print "  " $0}'`).
- Camera: consider the **focus** (actuator/AF) as a possible cause of bad frames.
- Compaction report file name is **`compaction.log.md`** (user first misspelled it "comaction.log.md", then corrected).
- `/tmp/opencode` assets: relocate the *needed* ones into the project dir; remove *stale* ones; **leave the kernel and other compiled object data in `/tmp/opencode`**.

## Decisions
- Camera thread = main thread; MicroPython/Krux in background, interleaved by the user.
- Keep the stock CAF kernel as long as possible; kernel changes last; the CPP timeout fix is publishable; debug instrumentation stays on `airgap/debug-capture`; `master` untouched.
- Camera work done by **observing the vendor's own behavior** under our kernel (kernel logs every ISP/CPP ioctl, every NFC packet, and the VFE registers) rather than guessing.
- MicroPython: mainline MicroPython 1.28 (not MaixPy) + **lift MaixPy's portable modules** and provide the rest as port modules; the port provides the MaixPy-named modules so the firmware needs few changes.
- Rootfs: buildroot is only a convenience for a static busybox; TWRP's busybox (bionic) is reused since the runtime *is* the TWRP ramdisk. Buildroot path currently broken.
- Init stub is a **service**, not PID 1: TWRP's init stays PID 1; the stub waits for adb, then decides (Vol+ → TWRP, else firmware).
- Display: the framebuffer is **triple-buffered** (1080×5760, stride 4352, 32 bpp); the module draws into the buffer the panel shows and pans the panel to it.
- LCD rotation semantics matched to MaixPy's *meaning* for a natively-portrait panel: rotation 1 = portrait (identity), 3 = portrait upside down, 2 = landscape quarter turn, 0 = landscape other way; the firmware's `PORTRAIT=1`, `LANDSCAPE=2`.
- Font scaled ×3 (8×16 VGA font → 24×48) to match the K210's text share on a 1080×1920 panel; `board.py` declares `font: (24,48)`, `font_wide: (48,48)`.
- Colour: the mdssfb driver's red/blue offsets are swapped relative to what the panel shows → R and B are exchanged in `lcd_pixel()`.
- Touch: real evdev events are delivered (type=3, codes 57/53/54/48/58) — the firmware's touch path, not the driver, is the suspect.
- Compaction log: implement as an **OpenCode plugin** (project-local, `.opencode/plugins/` or `.opencode/plugin/`) subscribing to the `session.compaction.ended` event (payload: `reason`, `text` = summary, `recent`, `model`, `cost`, `tokens`), writing to `<project>/compaction.log.md`; also the `session.hook("compaction", …)` hook exists in this build.

## Work State
### Completed
- **Camera**: frame arrives with pixels — three fixes: chromatrix sent **once per frame** (`cpp_send_chromatrix()`); CSID data type **0x2b** on channel 0 (decode 2; LUT `vc0 0x0000122b … cfg0 0x00000023 cfg1 0x00000013`); client buffers sized to the CAMIF window **3280×2464** (3264×2448 was 91,648 bytes too small → empty frame). Now `GOT A FRAME: index=2`.
- **MicroPython**: static arm32 MicroPython 1.28 builds and runs; `machine.I2C`, `machine.Timer/PWM/WDT/SDCard` all working (root cause of the earlier segfault: MicroPython 1.28 `mp_make_new_fun_t` is `(type, n_args, n_kw, args)`).
- **NFC transport** complete and verified against a real card, standalone in TWRP with no vendor HAL: `connect()` → `transmit(SELECT)` → `sw=6a82` (same answer the ROM's stack got).
- **Display module** `lcd_bcm.c` (fb0, 8×16 VGA font, RGB565, MaixPy API incl. `draw_qr_code_binary` — just added, MaixPy's exact algorithm: `starting_size=sqrt(len*8)`, `scale=max_width/(size+2)`, 1-module border, LSB-first bits).
- **`evdev_bcm.c`**: touch + keys via evdev with timeout; real touch events and KEY_VOLUMEUP(115) verified.
- **`qrcode` module**: MaixPy's own `qrcode.c` + Nayuki `qrcodegen` lifted into the port.
- **Firmware boots** on the port: splash → settings → pin check → **login page**; the **main menu is visible on the panel** (user confirmed).
- **Rotation fix**: screen went from blank/vertical-stripe to a correct portrait image with a readable UI (font then found "VERY tiny" → scaled ×3).
- **Log**: today's camera/firmware findings appended to `airgap-standalone.log.md` (now 9751 lines).

### Active
- **Display polish**: colour mapping (R/B swap just applied, not yet re-verified on the panel) and touch not responding in the firmware (evdev events confirmed arriving).
- **Camera**: frame has only ~10 rows of noise; CSID delivered 492 packets (4 % of a frame); `camif error status: 0xd0`; sensor CCI utility I2C fails `msm_sensor_cci_i2c_util line 538 rc = -22` every 2.4 s (actuator/AF never initialises).
- **Compaction log** (new): `compaction.log.md` + plugin not yet written.
- **Asset migration** (new): `/tmp/opencode` is 4.6 GB of 7.8 GB tmpfs; nothing relocated or deleted yet. Identified: `mp/` 2.4 G (micropython-1.28.0 1.2 G + pristine 1.2 G + tar.xz 105 M), `ko-cap/` 719 M (kernel build — **stays**), `user_de/` 480 M, `app/` 423 M (extracted APKs), `twrp-ram/`+`twrp3/` 96 M each, `armel-sysroot/` 71 M (needed: `SYSROOT` for the MicroPython build), `twrp2/` 59 M, `bb/` 41 M (busybox-1.38.0), `kcmp/` 29 M, `los/`+`hammer/` 17 M each, `kma/` 15 M, `rt.FNmR/` 7.1 M, `embit-src/` 5.1 M, `romcheck/` 8.4 M, `kcam/` 4.0 M, `romrd/` 2.2 M, `embit2/` 1.1 M, `seq/` 1.2 M, plus ~hundreds of loose scripts/logs/binaries (fb*.py, capture-*.sh, nfc*.py, build-*.sh, camprobe, dump, fbtest, fbio.c, fb3.c, …) not present in the project.
- **Session log**: a fresh camera/firmware update is still owed.

### Blocked
- Rootfs build (buildroot): musl toolchain absent from the dl cache; glibc host dir is mixed (host-gcc-final-15.3.0 deleted, stamps faked) → `make` fails in musl's configure. Deferred until a serial adapter exists.
- Krux's `image` module (OpenMV) and `k_quirc` not yet ported (MaixPy sources exist at `firmware/MaixPy/components/micropython/port/src/{omv,k_quirc}`).
- `krux/sd_card.py` needs `machine.SDCard` (present now; raises "this device has no SD card").
- `sensor` stub still missing module-level `set_pixformat` (firmware prints "Camera not found" and continues).

## Next Move
1. Write the project-local OpenCode plugin that appends a compaction report to `compaction.log.md` on `session.compaction.ended` (summary text, reason, timestamp, tokens), and create the file with a header.
2. Relocate the needed `/tmp/opencode` assets into the project (e.g. `standalone/assets/`): the armel sysroot (needed by `build-micropython.sh` via `SYSROOT`), the micropython tarball/pristine tree, the twrp ramdisk/boot-stub images (`twrp3/boot-stub.img` — the stub image is also at `standalone/rootfs/stock/boot-ourkern-clean-twrp.img`), the loose device-side helper scripts (`fb_crop.py`, `fb_dump3.py`, `nfc_test.py`, `nfc_port_test.py`, `capture-hal.sh`, `sweep.sh`, `twrp-run.sh`, `nfc_dbg.py`) and the small diagnostics; update `build-micropython.sh`'s `WORK`/`SYSROOT` defaults and any script that references `/tmp/opencode` paths.
3. Delete the stale `/tmp/opencode` bulk: `mp/pristine` (1.2 G, duplicate), `user_de/`, `app/`, `twrp2/`, `twrp-ram/`, `bb/`, `kcmp/`, `los/`, `hammer/`, `kma/`, `rt.FNmR/`, `romcheck/`, `romrd/`, `kcam/`, `embit-src/`; **keep** `ko-cap/` (kernel) and other compiled object data in `/tmp/opencode`.
4. Verify the display colour fix on the panel (background should be black with white text per default Krux) and check why the firmware's touch path ignores the arriving evdev events.
5. Update `airgap-standalone.log.md` with the camera findings (per-frame chromatrix, CSID 0x2b, buffer size, 10 rows of noise, camif 0xd0, AF -22) and the runtime/firmware state.
6. Camera: capture the vendor's **raw** state (a photo, not a preview, in the ROM's camera app under our kernel) and diff the VFE registers against ours; then investigate the 492-packet shortfall and the actuator's -22.
7. Finish "minimal firmware changes": provide `Maix` (GPIO) and `fpioa_manager` (`fm`) shims so the firmware's own `touch.py`/`buttons.py`/`light.py` work unmodified, and drop the port's replacements of those files.

## Relevant Files
- `/home/user/Develop/src/android-air-gapped/airgap-standalone.log.md`: the session log (9751 lines) — the place to record findings/chasges.
- `/home/user/Develop/src/android-air-gapped/standalone/kruxport/lcd_bcm.c`: display module — rotation mapping, ×3 font scale, `draw_qr_code_binary`, R/B swap just applied.
- `/home/user/Develop/src/android-air-gapped/standalone/kruxport/kruxport/board.py`: board config — now declares `font: (24,48)`, `font_wide: (48,48)`.
- `/home/user/Develop/src/android-air-gapped/standalone/kruxport/build-micropython.sh`: build script; defaults `SYSROOT=/tmp/opencode/armel-sysroot`, `WORK=/tmp/opencode/mp` — must be repointed after the migration.
- `/home/user/Develop/src/android-air-gapped/standalone/kruxport/apply-port.sh`: applies the port's files to the MicroPython tree (idempotent).
- `/home/user/Develop/src/android-air-gapped/standalone/kruxport/evdev_bcm.c`: touch/key input via evdev (events confirmed working).
- `/home/user/Develop/src/android-air-gapped/standalone/cam/imaging8.c`: imaging server (per-frame chromatrix, CAMIF-sized buffers, frame-rate print, buffer dumps).
- `/home/user/Develop/src/android-air-gapped/standalone/cam/STATE.md`: camera state log — pending update.
- `/home/user/Develop/src/android-air-gapped/standalone/rootfs/init-stub.c`: runtime stub (key check, mount /data, light panel, exec firmware or TWRP).
- `/home/user/Develop/src/android-air-gapped/standalone/rootfs/build.sh`: rootfs post-build (installs board.py, nfc.py, micropython, firmware, port file replacements).
- `/home/user/Develop/src/android-air-gapped/standalone/vendor/android_kernel_lge_hammerhead/`: kernel tree (branch `airgap/debug-capture`; uncommitted: msm_isp40.c VFE dump, msm_isp_util.c register dump + stream capture, msm_cpp.c timeout fix, camera.c format print, drivers/nfc/bcm2079x.c packet + ioctl logging).
- `/home/user/Develop/src/android-air-gapped/standalone/nfc/TRANSPORT.md`: NFC transport contract + `.ncd` firmware blobs.
- `/home/user/Develop/src/krux-sighash/krux/src/`: the firmware tree (branch `unified-sighash_seedkeeper`; `src/krux/`, `src/boot.py`, `vendor/embit` sighash fork).
- `/home/user/Develop/src/krux-sighash/krux/firmware/MaixPy/components/micropython/port/src/`: MaixPy sources (`qrcode/`, `omv/`, `k_quirc/`, `modules/`).
- `/home/user/Develop/src/android-air-gapped/standalone/rootfs/stock/`: boot images (incl. `boot-ourkern-clean-twrp.img`, `ramdisk-stub`).
- `/tmp/opencode/`: the tmpfs scratch area to migrate/prune — `mp/`, `armel-sysroot/`, `twrp3/`, `twrp-ram/`, `twrp2/`, `ko-cap/` (kernel, stays), plus loose helper scripts.

## Important Context
- Colour bug: the mdssfb driver reports red/blue offsets the other way round from the panel; `lcd_pixel()` now swaps them, and the read-back path was swapped to match. Default Krux is white text on black background.
- Krux rotation constants: `PORTRAIT, LANDSCAPE = [2, 3] if kboard.is_cube else [1, 2]` (`src/krux/display.py`), so the firmware calls `lcd.rotation(1)` for portrait.
- Framebuffer: `/dev/graphics/fb0` 1080×1920, 32 bpp, stride 4352, triple-buffered 1080×5760; `FBIOPAN_DISPLAY` may not be honoured → the module reads `var.yoffset` at init and also pans.
- Touch input devices: `/dev/input/event0` qpnp_pon, `event1` touch_dev, `event2` gpio-keys, `event3` hs_detect. Observed events: type=3, code 57 (TRACKING_ID) 16/-1, code 53 (X) 579, code 54 (Y) 937, code 48 (TOUCH_MAJOR) 5, code 58 (PRESSURE) 47.
- Camera constants: data type **0x2b** (channel 0, decode 2), channel 1 = 0x12 (decode 1), CAMIF 3280×2464, VC 0, CID 0, pixel clock 260 MHz requested (divider gives 266.67 MHz).
- Vendor HAL ioctls per frame: `ISP UPDATE_STREAM (0xc01056cd)`, `ISP ENQUEUE_BUF (0xc05056c2)`, `ISP VFE_REG_CFG (0xc00c56c0) ×7-8`, `CPP CFG (0xc01056c0)`, `CPP DQEVENT (0x80805659)`, `CPP GET_EVENTPAYLOAD (0xc01056c1)`, `ISP CONFIG_DONE (0xc00456ce)`.
- Kernel ioctl numbers: camera_v2 CPP ioctls come from `include/media/msmb_pproc.h`; NFC ioctls are `_IO(0xFA, nr)`: POWER_CTL 0x01, CHANGE_ADDR 0x02, READ_FULL_PACKET 0x03, SET_WAKE_ACTIVE_STATE 0x04, WAKE_CTL 0x05, READ_MULTI_PACKETS 0x06, SET_CLIENT_ADDR 0x07.
- Port's MicroPython build flags: `CROSS_COMPILE=arm-linux-gnueabihf- HOSTCC=gcc MICROPY_PY_BTREE=0 MICROPY_PY_USSL=0 MICROPY_PY_FFI=0 MICROPY_PY_CRYPTOLIB_CONSTS=1 MICROPY_PY_CRYPTOLIB_CTR=1 CFLAGS_EXTRA="--sysroot=/tmp/opencode/armel-sysroot" LDFLAGS_EXTRA="-static --sysroot=/tmp/opencode/armel-sysroot -lm"`; tree at `/tmp/opencode/mp/micropython-1.28.0` (host build for gdb: `make DEBUG=1` in ports/unix; ARM objects moved aside to build-standard-arm and restored).
- The firmware lives on the device at `/data/krux` (micropython, boot.py, krux/, board.py, nfc.py, embit/, sensor.py, pmu.py, secp256k1.py, hmac.py, uhashlib_hw.py) with the port's replacements of `krux/{touch,buttons,light,auto_shutdown,wdt,kboard}.py`.
- MicroPython limits worked around: C stack 40 KB → 500 KB (`ports/unix/main.c`); no extended slices (`b[::-1]`); no dotted relative imports; `hashlib` lacks ripemd160 and `new()`; `ucryptolib`/`hashlib` are built-ins a local file cannot shadow; `os.open`/`os.pread` do not exist; `print(..., flush=True)` unsupported.
- OpenCode build: v2.0.29 at `~/.opencode/bin/opencode`; API via `opencode api get|post <path>`; openapi at `/openapi.json` (local copy `/tmp/opencode/ocapi.json`). Plugin discovery dirs: `.opencode/plugin/` or `.opencode/plugins/` (project and global `~/.config/opencode/`). This build's plugin API is the V2 style (`Plugin.define({ id, setup(ctx) })`, `ctx.event.subscribe({signal})`, `ctx.session.hook("compaction", …)`); the events are `session.compaction.started|delta|ended|failed` with `session.compaction.ended` carrying `reason`, `model`, `text` (summary), `recent`, `cost`, `tokens`. The globally installed npm `@opencode-ai/plugin` is still 1.18.33 (old API) — prefer the built-in V2 API.
- Device: `/dev/bcm2079x` (nfc:nfc, 0660), `/dev/graphics/fb0`, `/dev/input/event0..3`; TWRP's `/sbin/recovery.stock` is the stock init; our stub runs as the `recovery` service (seclabel u:r:recovery:s0), mounts `/data` from `/dev/block/bootdevice/by-name/userdata`, lights the panel via `/sys/class/backlight/lm3630/brightness`.
- Disks: `/tmp` is tmpfs 7.8 G (4.6 G used); `/home` has 111 G with 48 G free — the project dir is the right place for the assets.

## 2026-10-21 03:27:26 UTC - session compaction (auto)

* session: `ses_ec9cfaf7efferMLQZnvjWxHDO8`

## Objective
- Air-gapped Nexus 5 (hammerhead): own kernel + minimal Linux userspace running the Krux firmware in MicroPython, with a working camera and a working SeedKeeper NFC transport.
- Immediate goal: slim the project directory — relocate needed assets out of tmpfs (`/tmp/opencode`), drop unused vendored toolchains after proving the host toolchain still builds a working kernel.

## Requirements
- Air-gapped: no modem/Wi-Fi/BT, no privileged/signature permissions, revertible by reflash.
- **Device access protocol**: one device, two threads, interleaved — camera needs fastboot/TWRP+our kernel; MicroPython work runs on the ROM's Android. Camera boot+run cycle is atomic; MicroPython work happens between cycles.
- **No serial console** → adb is the only debugging channel.
- **No own init**: reuse the TWRP ramdisk; full rootfs replacement deferred until a serial adapter exists.
- **Minimize changes to the Krux project** so changes can be merged upstream; prefer the MaixPy API surface over firmware edits.
- Runtime design: hide our firmware "under" TWRP — **the firmware is the default payload**, **holding Vol+ gives TWRP**; adb must start before our stub.
- Update the session log after each conclusion/report or change (log = `airgap-standalone.log.md`).
- Use `\7` (bell) for the user's attention for physical actions; keep command timeouts short; TWRP boots in ~20 s; avoid `sed 's/^/  /'` (use `awk '{print "  " $0}'`).
- Camera: consider the **focus** (actuator/AF) as a possible cause of bad frames.
- Compaction report file name is **`compaction.log.md`** (user first misspelled it "comaction.log.md", then corrected).
- `/tmp/opencode` assets: relocate the *needed* ones into the project dir; remove *stale* ones; **leave the kernel and other compiled object data in `/tmp/opencode`**.
- **"When relocating micropython use `BUILD=` flag to keep binary output in /tmp. The kernel builds already done that way, keep it the same."** — keep frequently rebuilt/reproducible objects on tmpfs for speed and project backup-snapshot efficiency.
- **Exclude `~/Develop/src/krux-sighash` and everything else in `~/Develop/src` except our project dir** (`~/Develop/src/android-air-gapped`).
- Toolchain plan: do one end-to-end image build (repeating the last one), then try the **host toolchain used for micropython** (`arm-linux-gnueabihf-gcc`); if the kernel still works, drop the two vendored toolchains in `standalone/vendor/toolchain` — **except `gcc-androideabi-4.9`, the one just moved there**.

## Decisions
- Camera thread = main thread; MicroPython/Krux in background, interleaved by the user.
- Keep the stock CAF kernel as long as possible; kernel changes last; the CPP timeout fix is publishable; debug instrumentation stays on `airgap/debug-capture`; `master` untouched.
- Camera work done by **observing the vendor's own behavior** under our kernel (kernel logs every ISP/CPP ioctl, every NFC packet, and the VFE registers) rather than guessing.
- MicroPython: mainline MicroPython 1.28.0 (not MaixPy) + **lift MaixPy's portable modules** and provide the rest as port modules; the port provides the MaixPy-named modules so the firmware needs few changes.
- Rootfs: buildroot is only a convenience for a static busybox; TWRP's busybox (bionic) is reused since the runtime *is* the TWRP ramdisk. Buildroot path currently broken.
- Init stub is a **service**, not PID 1: TWRP's init stays PID 1; the stub waits for adb, then decides (Vol+ → TWRP, else firmware).
- Display: the framebuffer is **triple-buffered** (1080×5760, stride 4352, 32 bpp); the module draws into the buffer the panel shows and pans the panel to it.
- LCD rotation semantics matched to MaixPy's *meaning* for a natively-portrait panel: rotation 1 = portrait (identity), 3 = portrait upside down, 2 = landscape quarter turn, 0 = landscape other way; the firmware's `PORTRAIT=1`, `LANDSCAPE=2`.
- Font scaled ×3 (8×16 VGA font → 24×48) to match the K210's text share on a 1080×1920 panel; `board.py` declares `font: (24,48)`, `font_wide: (48,48)`.
- Colour: the mdssfb driver's red/blue offsets are swapped relative to what the panel shows → R and B are exchanged in `lcd_pixel()`.
- Touch: real evdev events are delivered (type=3, codes 57/53/54/48/58) — the firmware's touch path, not the driver, is the suspect.
- Compaction log implemented as a project-local OpenCode plugin (`.opencode/plugins/compaction-log.ts`) subscribing to `session.compaction.ended` and writing `<project>/compaction.log.md`.
- **Source in the project, build output on tmpfs** — same split the kernel build has had all along (`--tree` in project, `--out` on tmpfs). Applied to MicroPython (`BUILD=`), the armel sysroot (rebuilt on demand from vendored `.deb`s), and the runtime stub.
- The **armel sysroot is rebuilt, not carried**: `standalone/tools/build-armel-sysroot.sh` unpacks `standalone/vendor/toolchain.ubuntu/*.deb` → 686 files, byte-for-byte the tree in use.
- MicroPython upstream source = the **`v1.28.0` tag** of `standalone/vendor/micropython` (verified to reproduce the pristine tree byte-for-byte, plus the two submodules `lib/mbedtls` @ `107ea89d` and `lib/micropython-lib` @ `8380c7bb`, fetched and cached in the project clone). The build takes a git worktree of the tag; no tarball, no network.
- The runtime-stub **recipe was written down** as three scripts (`build-stub-init.sh`, `build-stub-ramdisk.sh`, `make-boot-image.sh`); the init stub rebuilds **byte-for-byte identical** to the shipped one; the ramdisk rebuilds to the same *content* (the cpio carries inode numbers/mtimes, so it is not byte-reproducible — the check compares content and keeps the shipped bytes unless `--force`).
- `app/` and `user_de/` were **dumps of the device's live `/data`** (the device still has 50 packages in `/data/app`) — deleted as re-obtainable, keeping only the NFC reader APK.
- AOSP `prebuilts/gcc-androideabi-4.9` **moved to `standalone/vendor/toolchain/gcc-androideabi-4.9`**; `build/build-kernel.sh` PREBUILTS auto-detect updated; `TOOLCHAIN=aosp` and default `--check` both verified to resolve.

## Work State
### Completed
- **Compaction log**: `.opencode/plugins/compaction-log.ts` (project-local, discovered by `opencode plugin list`) writes `compaction.log.md`; verified end-to-end by triggering a real compaction via the API (8 sections recorded).
- **Asset migration**: `/tmp/opencode` went from **5.2 G (68% of tmpfs) to 1.1 G (14%)**; only rebuildable trees remain: `ko-cap/` (719 M, kernel objects), `mp-build/` (171 M, firmware build tree), `armel-sysroot/` (71 M, rebuilt from vendored `.deb`s). `df -h /tmp` → 7.8 G total, 6.8 G free.
- **Relocated into the project**: 144 session scripts → `standalone/scripts/` (grouped cam/nfc/display/build/boot/kernel/rootfs/misc + `config.sh` + README); 68 device-side probes → `standalone/probes/`; vendor capture data → `standalone/cam/data/vendor/{seq,cyc,sensor,ispif,csid,rom-libs}`; NFC reader app → `standalone/nfc/nfcreader.apk`; stub artifacts → `standalone/rootfs/stub/{krux-init,init.recovery.service.rc}`; `stock/boot-stub.img` + `stock/kernel-stub`; TWRP device tree → `standalone/vendor/twrp_android_device_lge_hammerhead`; kernel configs → `standalone/kernelcaps/configs/{ko-cap,hammerhead_defconfig,lineageos_hammerhead_defconfig}.config`; notes → `standalone/notes/`.
- **Deleted as stale/duplicated**: `app/`, `user_de/`, `mp/pristine/`, `twrp2/`, `twrpr/`, `bb/`, `kcmp/`, `los/`, `romcheck/`, `romrd/`, `rt.FNmR/`, `kma/`, `kcam/`, `embit-src/`, `embit2/`, `seq|sensor|ispif|csid|cyc|chromatrix/`, `qrx/`, `qrlib/`, `t1/`, `t2/`, `bk-out*/`, `cfgtest/`, `misc_ce/`, `tc-probe/`, `diag/`, `hammer/`, `kernel/`, `mp/` (old tree + tarball), old `mp-build-old`, plus 554 loose files and all hidden scratch (`.debtest`, `.v128`, `.x`, and my own `.a/.b/.ck1/.ck2/...`).
- **Build scripts rewritten/added**: `standalone/kruxport/build-micropython.sh` (now `SRC=`/`BUILD=`/`SYSROOT=`, git-worktree of the tag, auto-builds the sysroot, `git worktree prune` for stale registrations); `standalone/tools/build-armel-sysroot.sh`; `standalone/rootfs/build-stub-init.sh`; `standalone/rootfs/build-stub-ramdisk.sh`; `standalone/rootfs/make-boot-image.sh` (defaults to `stock/kernel-stub`, not ko-cap's zImage).
- **Verified end-to-end from the project with tmpfs wiped**: sysroot rebuilt → firmware rebuilt (1251400 bytes, same `.text`/`.data`/`.bss` as the shipped binary; only the banner says `-dirty`) → stub init byte-identical (`1aab05f3b9a9481ecb7f6f904b6fe253`) → stub ramdisk content-identical (2190 files, 1218 symlinks) → boot image 22646784 bytes with kernel `997ddf77` + ramdisk `4531ccb8`.
- **Bug found in the old `boot-stub.img`**: its header carried the *stock* TWRP ramdisk size (14595115) while storing ours (15820520) — the bootloader would have loaded a truncated ramdisk (3275 of 3494 members, losing `sbin/recovery`, TWRP's own binary = the Vol+ path). It had never been flashed (device still runs the stock ROM kernel `3.4.113-gda91d066bc6`), so nothing was lost; the rebuild is correct.
- Docs updated: `standalone/TOOLCHAIN.md` (new "The armel sysroot" section), `standalone/kruxport/README.md` (build model + table), `standalone/README.md` (new "3b. The runtime stub" section), `standalone/scripts/README.md`, `standalone/probes/README.md`, `airgap-standalone.log.md` Entry 81 (~9909 lines) documenting the migration, the `BUILD=` split, the deleted assets, the stale-`ramdisk_size` bug, and the cpio note.
- All 144 relocated `.sh` files re-verified to parse (`bash -n`), and their now-dead `/tmp/opencode/{chromatrix,seq,vendor,kernel}` references repointed at the project copies.

### Active
- **Toolchain slimming**: `prebuilts/gcc-androideabi-4.9` → `standalone/vendor/toolchain/gcc-androideabi-4.9` (done, 136 M, verified working). Remaining in `standalone/vendor/toolchain`: `usr/` (2.0 G, Arch GCC 16.2.0 — **the default path, and what built the current kernel**), `arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi/` (1.1 G, Arm GNU 15.2.Rel1 — the `TOOLCHAIN=armgnu` option, never used to build), `arm-none-eabi-gcc-16.2.0-1-x86_64.pkg.tar.zst` (268 M) + `arm-none-eabi-binutils-2.47-1-x86_64.pkg.tar.zst` (2.3 M) — verified that gcc+binutils archives unpack to exactly the `usr/` tree.
- Plan (per user): do one end-to-end image build repeating the last one → then try the **host toolchain** (`arm-linux-gnueabihf-gcc` 15.2.0) for the kernel and check the kernel still works → if it does, drop `usr/` and `arm-gnu-toolchain-15.2.rel1-…` (keeping `gcc-androideabi-4.9`).

### Blocked
- Kernel build with the host `arm-linux-gnueabihf-gcc` is untested for the 2013 CAF tree; `build/build-kernel.sh` warns "A 2013 CAF tree on GCC 15.2.0 may not compile", and the hammerhead reports AT_HWCAP_FP clear, so a hard-float compiler may emit VFP instructions that SIGILL — a soft-float flag may be needed.
- `/tmp/opencode/ko-cap` (13:40) is **stale** relative to the tree's later edits (msm_isp40.c 11:57, bcm2079x.c 12:28, msm_cpp.c 10:30) — it predates them, so it is not the "last" build.
- The kernel-stub in `stock/kernel-stub` (997ddf77, 16:34) differs from ko-cap's zImage-dtb (10f8d5a7, 13:40); the build dir that produced kernel-stub is no longer present.
- The device currently runs TWRP with the stock ROM kernel, not our stub image; our boot image has never been flashed.

## Next Move
1. Repeat the last end-to-end image build with the current toolchain: rebuild the kernel from the current tree (with its uncommitted camera/NFC edits) via `build/build-kernel.sh --out /tmp/opencode/ko-cap`, then `standalone/rootfs/build-stub-init.sh` → `build-stub-ramdisk.sh` → `make-boot-image.sh`; flash and verify adb + firmware on the device.
2. Rebuild the same tree with the **host toolchain** (`arm-linux-gnueabihf-gcc`; consider `-mfloat-abi=soft` given AT_HWCAP_FP clear) and verify the kernel still works (adb up, camera/NFC functional) — flash it the same way.
3. If the host-toolchain kernel works, delete `standalone/vendor/toolchain/usr/` and `standalone/vendor/toolchain/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi/` (keeping `gcc-androideabi-4.9`; decide whether to keep the two `.pkg.tar.zst` archives as the offline source for the Arch toolchain), and update `build/build-kernel.sh` so the default path no longer points at the removed `usr/`.
4. Record the outcome in `airgap-standalone.log.md` (Entry 82) and update `standalone/TOOLCHAIN.md`.

## Relevant Files
- `standalone/kruxport/build-micropython.sh`: firmware build with `SRC=`/`BUILD=`/`SYSROOT=`; the `BUILD=` pattern to reuse.
- `build/build-kernel.sh`: kernel build; `--tree`/`--out`, `TOOLCHAIN=arch|armgnu|aosp`, distro fallback; PREBUILTS detection now points at `standalone/vendor/toolchain`.
- `standalone/vendor/toolchain/`: the toolchains under discussion (`usr/` 2.0 G, `arm-gnu-toolchain-15.2.rel1-…` 1.1 G, `gcc-androideabi-4.9` 136 M just moved, two `.pkg.tar.zst`).
- `standalone/vendor/android_kernel_lge_hammerhead`: the kernel tree with uncommitted camera/NFC edits (msm_isp40.c, msm_isp_util.c, msm_cpp.c, camera.c, bcm2079x.c) — the input for both builds.
- `standalone/rootfs/{build-stub-init.sh,build-stub-ramdisk.sh,make-boot-image.sh}`: the runtime-stub recipe chain.
- `standalone/rootfs/stock/{boot-stub.img,kernel-stub,ramdisk-stub}`: the shipped runtime artifacts.
- `standalone/rootfs/init-stub.c` + `standalone/rootfs/stub/{krux-init,init.recovery.service.rc}`: the stub source and its built/patched files.
- `standalone/tools/build-armel-sysroot.sh`: rebuilds the sysroot from `standalone/vendor/toolchain.ubuntu/*.deb`.
- `standalone/tools/fetch-toolchain.sh`: fetches/extracts the Arch `arm-none-eabi-gcc`/`binutils` packages and the Arm GNU 15.2.Rel1 tarball.
- `standalone/kruxport/apply-port.sh`: applies the port onto the upstream tree.
- `standalone/cam/data/vendor/`: the captured vendor HAL configs and ROM camera libraries.
- `airgap-standalone.log.md`: the session log (Entry 81 = the migration; earlier entries = camera/NFC findings).
- `standalone/TOOLCHAIN.md`: toolchain documentation incl. the new armel-sysroot section.

## Important Context
- Toolchain roles: `usr/` = Arch GCC 16.2.0 (default; built the current ko-cap kernel, verified via `.cmd` files), `arm-gnu-toolchain-15.2.rel1` = Arm GNU 15.2.1 (the TWRP kernel's compiler; `TOOLCHAIN=armgnu`; never used to build), `gcc-androideabi-4.9` = AOSP GCC 4.9 (`TOOLCHAIN=aosp`; `arm-linux-androidkernel-gcc` is a symlink to `arm-linux-androideabi-gcc`; it cannot do a hosted link — bionic). The reference ROM kernel was built with plain GCC 16.2.0; TWRP's with Arm GNU 15.2.Rel1.
- The project is **not a git repo** (no `.git` at the project root; only nested trees have `.git`), so nothing is committed — all changes are on disk.
- `standalone/vendor/` is gitignored by convention ("vendored upstream trees -- keep on disk, do not track in git"); `standalone/rootfs/build/` (5.9 G buildroot output) and `standalone/buildroot/dl/` (1.2 G download cache) are also regenerable but still occupy space in `standalone/rootfs` (6.9 G) and `standalone/buildroot` (1.4 G) — not yet addressed.
- Host toolchain for micropython: `arm-linux-gnueabihf-gcc` (Ubuntu 15.2.0-16ubuntu1) with `--sysroot=/tmp/opencode/armel-sysroot`; the init stub builds with it and reproduces byte-for-byte.
- The cpio in the stub ramdisk is not byte-reproducible (inode numbers/mtimes); compare content with `diff -rq --no-dereference` (plain `diff` follows dangling symlinks and reports false differences).
- `opencode plugin list` shows the project-local `compaction-log` plugin; the compaction event payload carries `text` (the summary) and `recent`.
- Device: `0879524b0068abca` in recovery, adb up; `/data/krux/` holds the firmware tree; the runtime is still TWRP-with-stock-kernel, so the stub image has never been flashed.

## 2026-10-21 05:29:06 UTC - session compaction (auto)

* session: `ses_ec9cfaf7efferMLQZnvjWxHDO8`

## Objective
- Slim the project by dropping the two vendored toolchains (`standalone/vendor/toolchain/usr/` Arch GCC 16.2.0, and `arm-gnu-toolchain-15.2.rel1-*` Arm GNU 15.2.1) in favour of the host `arm-linux-gnueabihf-gcc` (the same toolchain the MicroPython firmware uses), keeping `gcc-androideabi-4.9`.
- Along the way, fix the boot-time regressions so the kernel+firmware actually work: display handover/commit, font scaling, colour order, touch input, and the panel being blanked.

## Requirements
- Air-gapped; no modem/Wi-Fi/BT, no privileged/signature permissions, revertible by reflash.
- **Device access protocol**: one device, two threads, interleaved — camera needs fastboot/TWRP+our kernel; MicroPython work runs on the ROM's Android. Camera boot+run cycle is atomic; MicroPython work between cycles.
- **No serial console** → adb is the only debugging channel.
- **No own init**: reuse the TWRP ramdisk; full rootfs replacement deferred until a serial adapter exists.
- Minimize changes to the Krux project so they can be merged upstream; prefer the MaixPy API surface over firmware edits.
- Runtime design: hide our firmware "under" TWRP — **the firmware is the default payload**, **holding Vol+ gives TWRP**; adb must start before our stub.
- Update the session log after each conclusion/report or change (log = `airgap-standalone.log.md`).
- Use `\7` (bell) for the user's attention for physical actions; keep command timeouts short; TWRP boots in ~20 s; avoid `sed 's/^/  /'` (use `awk '{print "  " $0}'`).
- Camera: consider the **focus** (actuator/AF) as a possible cause of bad frames.
- Compaction report file name is **`compaction.log.md`**.
- `/tmp/opencode` assets: relocate needed ones into the project, remove stale ones; **leave kernel/compiled object data in `/tmp/opencode`**.
- **"When relocating micropython use `BUILD=` flag to keep binary output in /tmp. The kernel builds already done that way, keep it the same."**
- Exclude `~/Develop/src/krux-sighash` and everything else in `~/Develop/src` except `~/Develop/src/android-air-gapped`.
- Toolchain plan: one end-to-end image build (repeating the last one), then try the **host toolchain** (`arm-linux-gnueabihf-gcc`); if the kernel still works, drop the two vendored toolchains — **except `gcc-androideabi-4.9`**.
- Images are tested with **`fastboot boot`** (never flashed/overwritten).

## Decisions
- Camera thread = main thread; MicroPython/Krux in background, interleaved by the user.
- Keep the stock CAF kernel as long as possible; kernel changes last; the CPP timeout fix is publishable; debug instrumentation stays on `airgap/debug-capture`; `master` untouched.
- Camera work: observe the vendor's own behaviour under our kernel (kernel logs every ISP/CPP ioctl, every NFC packet, the VFE registers) rather than guessing.
- MicroPython: mainline MicroPython 1.28.0 (not MaixPy) + lifted MaixPy portable modules + port modules providing MaixPy-named modules.
- Rootfs: buildroot only for a static busybox; TWRP's busybox (bionic) reused; buildroot path currently broken.
- Init stub is a **service**, not PID 1: TWRP's init stays PID 1; the stub waits for adb, then the Vol+ key.
- Display: the framebuffer is triple-buffered (1080x5760, stride 4352, 32 bpp); the module must draw into the plane the panel scans and commit it.
- Font scaled ×2 (8×16 VGA → 16×32); `board.py` must declare `font: (16, 32)`, `font_wide: (32, 32)` to match.
- Compaction log is a project-local OpenCode plugin (`.opencode/plugins/compaction-log.ts`) writing `<project>/compaction.log.md`.
- Source in the project, build output on tmpfs (`BUILD=`); the armel sysroot is rebuilt from vendored `.deb`s.

## Work State
### Completed
- Baseline end-to-end build repeated with the **Arch GCC 16.2.0** toolchain: kernel `/tmp/opencode/ko-base` (6825016 B zImage-dtb, md5 `914ebc80`), stub → ramdisk → boot image all succeed.
- **Proved the ramdisk was never the variable**: old `stock/boot-stub.img` and new `boot-base.img` carry a **byte-identical ramdisk** (`4531ccb8`, 15820520 B); only the kernel differs (old `997ddf77` = commit `e0cd9d48553`, Arm GNU 15.2.1; new = commit `e9bf776e758-dirty`, Arch GCC 16.2.0). The two kernels' `/proc/config.gz` **diffs to nothing**; the only source diffs are camera debug instrumentation + `init/main.c` SELinux-permissive patch + `selinuxfs.c`.
- **Root-caused the "stale TWRP frame"**: the framebuffer lives in reserved EBI1 memory (`qcom,mdss_fb_primary reserved EBI1 size 1000000`) that is **not cleared between boots** — the panel was showing the *previous* session's TWRP image.
- **Root-caused the missing display**: the panel scans a *copy* made by the driver's commit thread (`mdss_fb_display_commit` → `mdss_fb_pan_display_ex` → `__mdss_fb_display_thread` → `mdp3_ctrl_pan_display` re-points the panel's DMA). Without `MSMFB_DISPLAY_COMMIT` (`_IOW('m', 164, struct mdp_display_commit)`, 168 B) the panel never follows the framebuffer. After adding the commit, **the menu appears with text** (user confirmed).
- **Fixed the ioctl magic**: the port first used `0xAD`; the driver's magic is `'m'` (0x6D). Wrong magic → driver reads the struct at the wrong offset → scrambled panel. Also verified struct size 168 = `u32 flags` + `u32 wait_for_finish` + `struct fb_var_screeninfo` (160 B, identical in tree and host).
- **Fixed the stub's service name**: the stub *was* the `recovery` service, so its own `setprop ctl.stop recovery` killed **itself** (init: `Sending signal 9 to service 'recovery' (pid 194) process group...`), and init restarted it in a loop. Renamed the service to **`krux`** (`standalone/rootfs/stub/init.recovery.service.rc`); the panel-on/off cycling and the "[Touch] touch on/off" storm stopped; **volume buttons now navigate the menu** (user confirmed: "the volume battons react, menu selection jump from first item to last and back").
- **Fixed the font overlap**: deployed `board.py` with `font: (16, 32)`; firmware rebuilt with `LCD_FONT_SCALE 2`; user confirmed "The fonts are not overlaping now".
- **Answered the settings/config-file question**: Krux's `Store.get_vfs_location()` returns absolute `"/" + location + "/"`, i.e. `/flash/settings.json` and `/sd/settings.json`; the device has **neither** `/flash` nor `/sd`, so every settings write failed silently. The stub now creates `/flash -> /data/krux/flash` and `/sd -> /data/krux/sd` (verified present on device).
- **Fixed the missing firmware log**: init hands a service `/dev/null` on stdout/stderr; the stub now redirects stdout/stderr to `/data/krux/fw.log`. Firmware log now reads: `init-stub: starting the firmware` / `Camera not found: module 'sensor' has no attribute 'set_pixformat'` / `No pin set` (no "Terminated").
- Logged findings as **Entry 82** in `airgap-standalone.log.md` (now 10190+ lines).

### Active
- **Colour order fix (edited, not yet built/deployed)**: `lcd_bcm.c`'s `lcd_pixel()` now composes bytes in the panel's order (byte0=R, byte1=G, byte2=B, byte3=opaque) instead of using the driver's reversed offsets (`transp@0, blue@8, green@16, red@24`). Grounded in `fbcommit` composing `0xFFFF00FF` and the panel showing magenta. Must rebuild firmware, deploy, verify.
- **Touch input still dead**: direct reader on `/dev/input/event1` returned **0 events** over 12 s; the touch IRQ is `IRQF_TRIGGER_FALLING | IRQF_ONESHOT` (`request_threaded_irq(client->irq, NULL, touch_irq_handler, ...)`) and the touch IRQ is **not counted** in `/proc/interrupts` (only `msmgpio` entries, e.g. IRQ 240 = 117). Controller powers on (`[Touch] touch on`) but never interrupts. Note: my own `cat /dev/input/event1` background readers were stealing/consuming events earlier — they have been killed.
- **Panel blanking / "stale twrp frame"**: user attributes it to TWRP's charging animation (recovery draws it when not booted; "since the twrp is offline, the recovery switches to charging animation"; "after a few minutes charging animation switches the screen off"). Still to confirm which process issues the repeated `mdss_fb_ioctl: unsupported ioctl (801c6d02)` (a read ioctl, nr 2, 28 B, type 'm') that precedes each `mdss_dsi_panel_off`. Not found in `/sbin/recovery.stock` or `/sbin/charger` by byte-search.
- **Menu redraw is slow and flickers the whole screen** (user's words: "The redraw on menu selection chnage is slow and flickers the whole screen. It might be an issue when we get to showing camera output."). Likely because `lcd_commit()` runs after *every* drawing primitive; the commit is queued to a kernel thread and each queued commit copies a half-drawn frame. A deferred/once-per-frame commit was drafted then reverted; needs a cleaner design.

### Blocked
- Host-toolchain kernel build (`TOOLCHAIN=host`) not yet attempted; `arm-linux-gnueabihf-gcc` is installed (Ubuntu 15.2.0-16ubuntu1, 36 tools) and `arch/arm/Makefile` forces `-msoft-float` itself, so a hosted compiler is viable for the kernel — but this is unverified for the 2013 CAF tree, and the device reports `AT_HWCAP_FP` clear, so VFP would SIGILL.
- Touch: the controller powers on but its IRQ never fires, so no events reach any reader. Root cause not yet identified.

## Next Move
1. Rebuild the firmware with the new `lcd_pixel()` colour order (`bash standalone/kruxport/build-micropython.sh`), push to `/data/krux/micropython`, and verify the menu now shows black bg / white text (Krux's default theme is black/white; the "red background" was the alpha byte landing in the panel's red channel).
2. Fix the slow/flickering redraw: commit once per rendered frame rather than per drawing primitive (the commit is queued asynchronously to `__mdss_fb_display_thread`).
3. Chase the touch IRQ: find why `client->irq` never fires (check `ts->pdata->irq_gpio`, the `touch_int` GPIO, and whether the panel blanking/`touch_power_cntl` path disables the controller); also stop any leftover readers of `/dev/input/event1`.
4. Identify the process issuing `0x801c6d02` on fb0 that precedes each `mdss_dsi_panel_off` (search all ramdisk binaries, including `/sbin/charger` under its real name, and check `init.recovery.hlthchrg.rc`'s `service charger /charger -r`).
5. Then build the kernel with `TOOLCHAIN=host` into `/tmp/opencode/ko-host`, boot it with `fastboot boot`, and verify menu/colour/touch; if it works, delete `standalone/vendor/toolchain/usr/` (2.0 G) and `standalone/vendor/toolchain/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi/` (1.1 G), keeping `gcc-androideabi-4.9` (136 M), and record it as Entry 83.

## Relevant Files
- `standalone/kruxport/lcd_bcm.c`: display module — the colour-order fix (edited, unbuilt), `lcd_commit()` / `MSMFB_DISPLAY_COMMIT` (added), `LCD_FONT_SCALE 2` (changed 3→2), pan/planes logic.
- `standalone/rootfs/init-stub.c`: the stub — added `make_the_settings_directories()` (`/flash`, `/sd` symlinks) and `redirect_the_output()` (`/data/krux/fw.log`); `ctl.stop recovery` now targets TWRP's service, not the stub's own.
- `standalone/rootfs/stub/init.recovery.service.rc`: service renamed `recovery` → **`krux`** (the fix that stopped the self-kill/restart loop).
- `standalone/kruxport/kruxport/board.py`: `font: (16, 32)`, `font_wide: (32, 32)` — must match `LCD_FONT_SCALE`; deployed to `/data/krux/board.py`.
- `standalone/kruxport/kruxport/touch.py`: port touch module — `_poll()` reads `ABS_MT_*` from `/dev/input/event1`; `TOUCH_S_PERIOD = 20`.
- `standalone/vendor/android_kernel_lge_hammerhead/drivers/input/touchscreen/touch_synaptics_ds5.c`: touch driver — `touch_irq_handler` (line 307), `touch_power_cntl` (331), `request_threaded_irq` with `IRQF_TRIGGER_FALLING|IRQF_ONESHOT` (~1814), `lcd_notifier_callback` (1652) which powers the touch off on `LCD_EVENT_OFF_END`.
- `standalone/vendor/android_kernel_lge_hammerhead/drivers/video/msm/mdss/mdss_fb.c`: fb driver — `mdss_fb_display_commit` (2201), `mdss_fb_pan_display_ex` (1543), `__mdss_fb_display_thread` (1673), `__mdss_fb_perform_commit`.
- `standalone/vendor/android_kernel_lge_hammerhead/drivers/video/msm/mdss/mdp3_ctrl.c`: `mdp3_ctrl_pan_display` (689) — re-points the panel's DMA at the fb plane; `mdp3_fb_stride` (970) → 4352.
- `standalone/rootfs/fbcommit.c`: the proven-working display tool — composes `0xFFFF00FF` (RGBX) into both planes then commits; establishes the panel's byte order.
- `standalone/scripts/capture-run.sh` + `standalone/scripts/fb2png.py`: run capture and raw-framebuffer→PNG renderer (the machine-readable oracle for what was drawn).
- `standalone/kruxport/build-micropython.sh`: firmware build (`SRC=`, `BUILD=`, `SYSROOT=`).
- `standalone/vendor/krux/src/krux/themes.py`: theme colours; `RED = 0x00F8` is **cyan** in RGB565 (red is `0xF800`) — upstream colour table.
- `standalone/vendor/krux/src/krux/pages/screensaver.py`: the marching-band animation that was mistaken for a screen-saver artifact.
- `standalone/vendor/krux/src/krux/input.py`: firmware input loop (`touch_event()`, `BUTTON_WAIT_PRESS_DELAY = 10`).
- `airgap-standalone.log.md`: session log; Entry 81 = asset migration, Entry 82 = the display/commit/colour/service-name findings.
- `standalone/vendor/toolchain/`: the two toolchains to be dropped plus `gcc-androideabi-4.9` to keep.

## Important Context
- Device `0879524b0068abca`; adb state `recovery`; images are loaded with `fastboot boot <img>`, never written to the device's partitions.
- Framebuffer: `xres=1080 yres=1920 yres_virtual=5760 bpp=32`, `red@24 green@16 blue@8 transp@0`, `line_length=4352`, `smem_len=16777216`. Driver-reported offsets are **reversed** relative to the panel; the panel's order is byte0=R, byte1=G, byte2=B, byte3=X (proved by `fbcommit`'s magenta).
- The framebuffer memory is reserved EBI1 and **survives reboots** — hence "stale TWRP frame".
- The panel follows the fb only after `MSMFB_DISPLAY_COMMIT` (`0x40a86da4`); the commit is executed by a kernel thread, so it is asynchronous.
- Krux's settings store uses absolute `/flash/` and `/sd/`; `Store.get_vfs_location()` = `"/" + location + "/"`.
- The old kernel `997ddf77` is commit `e0cd9d48553` ("usb: f_fs: queue a full-size request for reads"), which is **not** an ancestor of HEAD and is not on any branch; it contains the adb USB fixes (`846ed801cb1`, `e0cd9d48553`). The current `airgap/debug-capture` HEAD lacks `e9f1372377a` ("usb: make the adb transport work on this device"), which lives on `airgap/wip-twrp-defconfig`.
- The repo is **not** a git repo at the top level; only nested vendor trees have `.git`. Nothing is committed at the project level.
- Adb over USB works on this kernel; the firmware runs on the ROM's Android for camera work.
- `standalone/rootfs/build/` (5.9 G) and `standalone/buildroot/dl/` (1.2 G) remain unaddressed for slimming.
- The device's `/data/krux` holds the deployed firmware (`micropython`, `board.py`, `boot.py`, `krux/`, `embit/`, `fw.log`); `board.py` was overwritten by the ×2 version.

## 2026-10-21 09:23:38 UTC - session compaction (auto)

* session: `ses_ec9cfaf7efferMLQZnvjWxHDO8`

## Objective
- Slim the project by dropping the two vendored toolchains now that the kernel builds with the host `arm-linux-gnueabihf-gcc`, keeping only `gcc-androideabi-4.9`; and fix the remaining runtime defects (touch, redraw speed/flicker, colours, brightness, power-off) on the hammerhead.

## Requirements
- Air-gapped; no modem/Wi-Fi/BT, no privileged/signature permissions, revertible by reflash.
- One device, two threads, interleaved: camera needs fastboot/TWRP + our kernel; MicroPython work runs on the ROM's Android. Camera boot+run cycle is atomic; MicroPython work between cycles.
- No serial console → adb is the only debugging channel. No own init: reuse the TWRP ramdisk; full rootfs replacement deferred until a serial adapter exists.
- Minimize changes to the Krux project so they can be merged upstream; prefer the MaixPy API surface over firmware edits.
- Runtime design: hide our firmware "under" TWRP — firmware is the default payload; holding Vol+ gives TWRP; adb must start before our stub.
- Update the session log (`airgap-standalone.log.md`) after each conclusion/report or change.
- `\7` means the ASCII BEL character (0x07) placed **in the chat text before** the command that needs the user's action — not the literal characters, and not inside command output.
- Keep command timeouts short; TWRP boots in ~20 s; avoid `sed 's/^/  /'` (use `awk '{print "  " $0}'`).
- Images are tested with `fastboot boot` (never flashed/overwritten).
- Device: `0879524b0068abca`; host toolchain `arm-linux-gnueabihf-gcc` 15.2.0.

## Decisions
- Host toolchain builds the kernel successfully → the two vendored toolchains (Arch GCC 2.0 G, Arm GNU 1.1 G) can be deleted; keep `gcc-androideabi-4.9` (136 M) for the MicroPython build.
- Camera work: observe the vendor's behaviour under our kernel rather than guessing.
- MicroPython: mainline 1.28.0 + lifted MaixPy portable modules + port modules providing MaixPy-named modules.
- Rootfs: buildroot only for a static busybox; TWRP's busybox reused; buildroot path currently broken.
- Init stub is a service named `krux` (not `recovery`, which would make `ctl.stop recovery` kill the stub's own process group).
- Firmware source in the project, build output on tmpfs via `BUILD=`; armel sysroot rebuilt from vendored `.deb`s.
- Keep the stock CAF kernel as long as possible; kernel changes last; the CPP timeout fix is publishable; debug instrumentation stays on `airgap/debug-capture`; `master` untouched.
- Colour fix: the theme's constants are little-endian RGB565 words → byte-swap before decoding; panel byte order is R,G,B,X (proved by fbcommit's magenta).
- `TOTAL_LINES` (vertical line count) must derive from the board's declared **width** = the long side (1920) for our portrait panel.
- Display commit deferred to page end; the port's input modules call `lcd.flush()` while polling.

## Work State

### Completed
- **Host-toolchain kernel**: built at `/tmp/opencode/ko-host` (zImage-dtb 7176744 B) and **boots and runs the firmware** (`Linux version 3.4.113-ge9bf776e758-dirty (gcc version 15.2.0 (Ubuntu 15.2.0-16ubuntu1))`). Image `/tmp/opencode/boot-hostkernel.img` (template `stock/boot-ourkern-clean-twrp.img`, ramdisk `stock/ramdisk-stub`).
- **Brightness 50 %**: stub now writes `max_brightness/2` (127 of 255) by reading the driver's `max_brightness`; verified live and after reboot.
- **Power-off fixed**: `machine.reset` was missing from the port (unix port has `MICROPY_PY_MACHINE_RESET=0`), so `machine.reset()` raised AttributeError, the firmware exited status 1 and init restarted the service. Added `machine.reset()` to `machine_hw_linux.c` using the phase-0 route: `setprop sys.powerctl shutdown` (init.rc `on property:sys.powerctl=* → powerctl`). Registered via `apply-port.sh` globals + `MP_DECLARE_CONST_FUN_OBJ_0(machine_reset_obj)` in `extmod/modmachine.h`. User confirms shutdown works.
- **Colours**: byte-swap fix in `lcd_pixel()`; verified RED→(255,0,0), GREEN→(0,255,0), CYAN→(0,255,255), WHITE/BLACK exact. Top-right symbol is the battery indicator, now green.
- **Redraw speed**: `lcd_fill_rect` was per-pixel via `lcd_set_pixel` (159 ms full clear) → now maps the rect once and fills row-wise: clear 159 ms → 3.5 ms, fill_rectangle 123 ms → 1.7 ms. User confirms performance is good.
- **Flicker gone**: commit deferred (one per page instead of per primitive) — user confirms.
- **Screen saver covers the whole screen**: `board.py` now declares `lcd` width 1920 / height 1080 so `TOTAL_LINES`=60, `BOTTOM_LINE`=1888. User confirms full screen and faster.
- **Vol+ key**: was a stuck key from an earlier session, not a bug; works after reboot (IRQ 318 = 22). ROM DT identical to ours.
- **Heat**: my leftover watcher loop (`/proc/*/fd/*` scan) was the main cause — killed; load average 2.18 → 0.28.
- **Touch root cause found**: the port's `evdev` module (`standalone/kruxport/evdev_bcm.c`) did **not** export `ABS_MT_TRACKING_ID` (0x39) nor `ABS_MT_PRESSURE` (0x3a). The touch module references `evdev.ABS_MT_TRACKING_ID`, so the first tracking event raised AttributeError, swallowed by the firmware's input loop. Kernel/driver/DT/IRQ all verified correct (driver logs `pos( 480, 951)`, `Interrupt_status : 0x4`; raw node yields TRACKING_ID/X/Y/PRESSURE).
- Log entries 83, 84, 85 written to `airgap-standalone.log.md` (now 10775 lines).

### Active
- **Touch still not reaching the firmware** even after adding `ABS_MT_TRACKING_ID` and rebuilding/deploying. The port's `Touch._poll()` now works standalone (reports real coords, e.g. x=634 y=1007), so the remaining gap is in how the firmware's input loop consumes it. Upstream semantics being compared: upstream `krux/touch.py::event()` gates on `current_time > self.sample_time + TOUCH_S_PERIOD` and calls `self.touch_driver.event()`; the firmware's `_wait_for_press` calls `flush_events()` (which calls `touch_event()`) before its loop, and `flush_events()` discards the result — a held finger's TRACKING_ID is consumed by the flush and never re-reported while the finger stays down.

### Blocked
- (none)

## Next Move
1. Read upstream `standalone/vendor/krux/src/krux/touch.py` `event()`/`current_state()` and the firmware's `flush_events()` ordering to make the port's `event()` match upstream's "one event per press, re-armed on release" semantics so a held finger is not swallowed by `flush_events()`.
2. Adjust `standalone/kruxport/kruxport/touch.py` accordingly (e.g. keep a `pressed`/`reported` flag cleared only on finger-up), rebuild with `bash standalone/kruxport/build-micropython.sh`, push to `/data/krux/micropython`, restart the firmware, and re-test the touch with the user.
3. Delete the two vendored toolchains (`standalone/vendor/toolchain/usr/` 2.0 G, `standalone/vendor/toolchain/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi/` 1.1 G), keeping `gcc-androideabi-4.9`; note `build/build-kernel.sh` lines 292/304 reference the deleted paths and `standalone/scripts/kernel/cmp-kernels.sh` uses the Arm GNU objdump — update or keep a stub.
4. Update `airgap-standalone.log.md` with the final touch resolution and the toolchain removal.

## Relevant Files
- `standalone/kruxport/evdev_bcm.c`: port's evdev module; **missing `ABS_MT_TRACKING_ID`/`ABS_MT_PRESSURE` were the touch bug** (now added).
- `standalone/kruxport/kruxport/touch.py`: port touch module; `_poll()`/`event()`/`current_state()` — the current active fix area.
- `standalone/vendor/krux/src/krux/touch.py`: upstream reference for `event()` semantics.
- `standalone/vendor/krux/src/krux/input.py`: firmware input loop; `flush_events()`, `_wait_for_press()`, `touch_event()`.
- `standalone/kruxport/lcd_bcm.c`: display module; `lcd_pixel()` byte-swap, fast `lcd_fill_rect`, deferred `lcd_do_commit`/`lcd_flush`, `get_pixel`/`save_ppm` panel order.
- `standalone/kruxport/kruxport/buttons.py`: port buttons module; calls `lcd.flush()` per poll.
- `standalone/kruxport/kruxport/board.py`: port board config; `lcd` width 1920/height 1080, `TOUCH_IRQ` = `/dev/input/event1`.
- `standalone/kruxport/machine_hw_linux.c`: port machine module; new `machine.reset()` (power-off via `setprop sys.powerctl shutdown`).
- `standalone/kruxport/apply-port.sh`: registers the port's machine-module globals (now including `reset`).
- `standalone/rootfs/init-stub.c`: init stub; brightness now `max_brightness/2`; service `krux`; `KEYS_NODE` = `/dev/input/event2`, `KEY_WINDOW_MS` 4000, `USB_WAIT_S` 8.
- `standalone/rootfs/stub/init.recovery.service.rc`: `service krux /sbin/krux-init`.
- `standalone/vendor/android_kernel_lge_hammerhead/drivers/input/touchscreen/touch_synaptics_ds5.c`: LGE touch driver (ds5) — verified correct.
- `standalone/vendor/android_kernel_lge_hammerhead/drivers/input/evdev.c`: evdev (packet_head advances only on SYN_REPORT).
- `standalone/vendor/krux/src/krux/themes.py`: theme colours are byte-swapped RGB565 (RED 0x00F8, GREEN 0xE007, BLUE 0xF800).
- `standalone/vendor/krux/src/krux/display.py`: `TOTAL_LINES = board.config["lcd"]["width"] // FONT_HEIGHT`.
- `build/build-kernel.sh`: kernel build script (`TOOLCHAIN=host` works); references the vendored toolchain paths.
- `airgap-standalone.log.md`: session log (entries 83–85 written).

## Important Context
- The host-toolchain kernel is `e9bf776e758-dirty`; the old stub kernel is `e0cd9d48553` (Arm GNU 15.2.1) and contains the adb USB fixes (`846ed801cb1`, `e0cd9d48553`); the ROM kernel `ge4e483b4ef6` lacks them → adb goes "offline" and the device needs a physical power cycle to recover.
- Framebuffer: 1080x1920, stride 4352, 32 bpp, `smem_len` 16777216, yres_virtual 5760 (triple buffered); driver reports transp@0 blue@8 green@16 red@24 but the **panel order is byte0=R, byte1=G, byte2=B, byte3=X** (fbcommit composed 0xFFFF00FF → magenta).
- `MSMFB_DISPLAY_COMMIT` = `_IOW('m', 164, struct mdp_display_commit)` (168 B; magic 'm' not 0xAD). A commit with `wait_for_finish` waits for the panel's vsync (~16 ms) — that was the flicker/slowness cause.
- The touch IC is alive and correct: driver identifies `PLG218 - 1 fw_version[E079]`; INT line = msmgpio 5, `interrupts = <5 0x2>`; IRQ 362 `msmgpio s3350`; `gpio-5 (touch_int)` idles high and the driver treats high as "INT STATE HIGH" → `-EINTR`, which `touch_irq_handler` silently drops (no `-EINTR` case).
- The touch controller is **powered off** whenever the panel blanks (`LCD_EVENT_OFF_END` → `synaptics_ts_stop`), and the panel blanks roughly every 470 s (`mdss_dsi_panel_off`); the last dmesg line may be `[Touch] touch off` with `gpio-5` stuck `lo`.
- `input_event` is 16 bytes on 32-bit ARM (timeval first, then type/code/value); the port's evdev struct matches.
- Device disk: `/data` is 90 % full (26 G, 2.6 G free); `/data/local/tmp` held 765 M of my debris, mostly removed.
- The kernel's `CONFIG_COMPAT` is not set, so `input_event_size()` = 16 and the ABS bitmap in `/proc/bus/input/devices` prints as two words, high first (`ABS=6618000 0` → codes 0x2f, 0x30, 0x35, 0x36, 0x39, 0x3a).
- The project is not a git repo at the top level; only nested vendor trees have `.git` (kernel tree at `standalone/vendor/android_kernel_lge_hammerhead`, krux at `standalone/vendor/krux`).
- My adb-side debris to clean up: `/data/local/tmp/{touchtest,probe*,ev*,imp,kr,tt,tt2,flushtest,coltest*,speed*,verify,keytest*,fingertest,ft2,gpiowatch,watch,tapdiff,cpurate,androkey}.py|.sh`.

## 2026-10-21 17:33:15 UTC - session compaction (auto)

* session: `ses_ec9cfaf7efferMLQZnvjWxHDO8`

## Objective
Make the LG Nexus 5 (hammerhead) run the Krux firmware standalone on our own kernel: working touch, working keys, a working camera, and a slim, reproducible project.

## Requirements
- Air-gapped; no modem/Wi-Fi/BT, no privileged/signature permissions, revertible by reflash.
- One device, two threads: camera needs fastboot/TWRP + our kernel; MicroPython work runs on the ROM's Android. Camera boot+run cycle is atomic.
- No serial console → adb is the only debugging channel. No own init: reuse the TWRP ramdisk; full rootfs replacement deferred.
- Minimize changes to the Krux project so they can merge upstream; prefer the MaixPy API surface over firmware edits.
- Runtime design: hide our firmware "under" TWRP; firmware is the default payload; holding Vol+ gives TWRP; adb must start before our stub.
- Update `airgap-standalone.log.md` after each conclusion/report or change.
- `\7` means the ASCII BEL character (0x07) placed **in the chat text before** the command that needs the user's action.
- Keep command timeouts short; avoid `sed 's/^/  /'` (use `awk '{print "  " $0}'`).
- Images are tested with `fastboot boot` (never flashed/overwritten).
- **User's list (session of 2026-10-21):** (0) free /tmp space, relocate the toolchain, drop stale kernel objects; (1) camera is highest priority, craft tests that need no user action; (2) fonts 150–200 %; (3) review the device-wipe function so it cannot brick the device; (4) decide what is needed to build from scratch + a quick build guide; (5) review `work/` and `tmp/`; (6) move kernel-related `tools/` objects to `standalone/tools`; (7) move `patches/linux/` under `standalone`; (8) document each directory; (9) backlight off when the screen blanks; entropy from the CPU; HardFloat ABI note; (10) define a Krux runtime data directory for secrets/settings; (11) test switching to HardFloat; (12) why no splash/logo at start; (13) final report: why the DTB fix was necessary now but not before.
- **User:** "try not to repeat the scenario when the device is unreachable by adb or fastboot"; "Please continue performing the tasks without stopping"; "good. please continue with the camera work".
- **User:** build trees belong back in `/tmp` (can afford ~10 % of /tmp for the kernel build); the toolchain may stay on disk.

## Decisions
- The touch and the volume keys were broken by **our own port**, not the kernel: `buttons.py` had `PRESSED/RELEASED` inverted vs upstream, so `krux/input.py`'s integrity check disabled the touchscreen at start-up; the volume keys needed `qcom,master-en` in the device tree.
- Our device tree described **none** of the pm8941's 36 pins, so the driver inherited them from the PMIC's volatile registers; ours is now the vendor's DTB + `master-en` on the two key pins (committed `acb586400b5`).
- The kernel is built with the **vendor's gcc 16.2.0** (Arch, from the `.pkg.tar.zst` archives, unpacked to `/home/user/Develop/toolchains/arch-16.2.0`); the host compiler is not the cause of the key fault.
- The camera fault is **in our kernel**: the ROM's own camera app under our kernel maps none of its 2.3 M packets, while the ROM's kernel produces a real 2448×3264 photo. The image's packets arrive on the **wrong virtual channel**.
- The camera's remaining gap is the CSID's configuration in our driver, to be compared with the ROM's on-device (ROM kernel `ge11370a8eb99` is not in our tree).
- Camera experiments that crash the kernel are **known crashers**; do not retry without a way back.
- The wipe is made safe by a port `flash` module that touches only the settings directory and refuses any address outside the SPIFFS area.
- Hardfloat is safe on this device (`AT_HWCAP_FP` is set); this **corrects** the earlier "assume no VFP" note in the log.

## Work State
### Completed
- **Touch works**: port `touch.py` reimplemented with the upstream region API (`y_regions`, `current_index`, `_extract_index`); `buttons.py` constants corrected to `PRESSED = 0` / `RELEASED = 1`; user confirmed touch selection.
- **Both volume keys work** from a cold boot: `qcom,master-en = <1>` added for `gpio@c100`/`gpio@c200` in `msm8974-hammerhead.dtsi`; verified `master_en=1` on both pins.
- **Kernel tree commits** (branch `airgap/pmic-keys`): `1bf1e0fa503` (keys' master-en), `21fa5da8858` (`msm_cpp` NULL firmware fix), `acb586400b5` (pm8941 pins, the vendor's configuration).
- **Camera fixes verified on device**: ION heap masks corrected (heap **ids**, system = id 30 → `1 << 30`); CPP firmware oops fixed; CPP firmware 1.2.0 loaded from `/lib/firmware` (chromatrix now **accepted**, status 0, frame arrives).
- **Fonts 150 %**: `LCD_FONT_SCALE 3` (24×48) + `board.py` font `(24,48)`/`font_wide (48,48)`; screenshot confirms.
- **Splash**: display module gained an idle-commit pthread (the deferred commit never happened during boot, so the logo never reached the panel).
- **Backlight**: upstream adds 7 to the brightness level (K210 PMU scale 7–15); port `pmu.py` now maps 7–15 → 0–8, plus `set_backlight_level()`; port `auto_shutdown` turns the backlight off after 30 s idle and back on at first input (the kernel's fb blanking already dims to 32).
- **Wipe reviewed and made safe**: port `flash.py` lives at `/data/krux/flash/__init__.py` (the settings *directory* shadows a module of the same name); erase removes `settings.json` + `seeds.json`; write raises; out-of-area addresses raise.
- **HardFloat measured**: `AT_HWCAP = 0xfb8d7` (FP yes, ASIMD yes); a hardfloat binary's double division works; all our binaries are hardfloat.
- **Entropy measured**: pool 4096, `entropy_avail` 219–1330, no `/dev/hwrng`, no CPU RNG instruction (ARMv7) → the camera's sensor noise is the source.
- **Project slimming**: 3.1 GB of vendored toolchains deleted; kept `gcc-androideabi-4.9` (136 MB) + Arch's two `.pkg.tar.zst` (270 MB); `TOOLCHAIN_PATH` added to `build/build-kernel.sh`; `TOOLCHAIN=host` is now the default.
- **Moves**: kernel tools → `standalone/tools/` (+ `profiles/`), camera generators → `standalone/cam/tools/`, `patches/linux/` → `standalone/patches/linux/`, project `tmp/` deleted and gitignored.
- **New documents**: `standalone/BUILD-GUIDE.md`, `DIRECTORIES.md`, `standalone/RUNTIME-DATA.md`, `standalone/scripts/boot/boot-our-firmware.sh`.
- **Log**: entries 86, 87, 88 + the camera/virtual-channel section and the "device needs a power-cycle" note (11 654 lines).
- **Android boots with our kernel** (project milestone): `boot-android-cam.img` with the ROM's Android ramdisk.

### Active
- **Camera: the virtual channel.** The image's packets carry the byte `0x01` after the data type while the CSID LUT (vendor HAL) maps VC0→0x2b, VC1→0x12; the 0x2b (image) packets are unmapped → empty frame. The ISP-IF takes a *mask* of channels (imaging server now supports `two_cids`); routing only cid 1 **crashes the kernel** (NULL deref, `msm_ispif` path); both cids does not crash but the frame stays empty.
- Imaging server gained `poke_reg`/`poke_val` (a sensor register written after the vendor sequence) and `two_cids` — built as `standalone/cam/imaging9`.

### Blocked
- The ROM's kernel source (`ge11370a8eb99`, `ge4e483b4ef6`) is not in our tree → the CSID driver cannot be diffed directly; must be disassembled on-device from the ROM kernel image + kallsyms (`standalone/scripts/kernel/cmp-kernels.sh`).
- The ROM's Android has no `/dev/mem` and no root (`su` absent, `adb root` disabled) → its CSID registers cannot be read from the shell.
- `standalone/cam/` and the project root are not git repositories; only the kernel tree and the vendored upstream trees are.

## Next Move
1. Fix the ISP-IF crash for a single non-zero virtual channel in our driver (it is a NULL deref in a path nothing else exercises) before any further VC experiment.
2. Compare the CSID's configuration with the ROM's: disassemble the ROM kernel's CSID functions (`msm_csid_config`, the LUT writer, `CORE_CTRL` writes) with `standalone/scripts/kernel/cmp-kernels.sh`, and read the CSID registers (base `0xfda08000`, offsets in `msm_csid_hwreg.h`) with the ROM's kernel running, where the camera works.
3. Sweep the sensor's VC register with the imaging server's new `poke` argument (candidates `0x0301,0x0303,0x0305,0x0309,0x030b,0x030c,0x0101,0x0102,0x0106`) and watch the CSID's captured long-packet header for the VC byte.
4. Make the firmware's camera page work: `Camera not found: module 'sensor' has no attribute 'set_pixformat'` (the port's `sensor.py` is a stand-in) — implement it once the camera works.
5. Re-check the remaining user items: splash verification by the user, the runtime-data directory (secrets are plain JSON, only mnemonics encrypted), the entropy path, and the final report.

## Relevant Files
- `/home/user/Develop/src/android-air-gapped/standalone/cam/STATE.md`: the camera's state on one page (what is configured, what is missing, the VC finding, the next step).
- `/home/user/Develop/src/android-air-gapped/standalone/cam/imaging8.c`: our imaging server (the port modules in `kruxport/` are pushed to the device).
- `/home/user/Develop/src/android-air-gapped/standalone/vendor/android_kernel_lge_hammerhead/arch/arm/boot/dts/msm8974-hammerhead/msm8974-hammerhead.dtsi`: the volume keys' `master-en` and the pm8941 pin overrides.
- `/home/user/Develop/src/android-air-gapped/standalone/vendor/android_kernel_lge_hammerhead/arch/arm/boot/dts/msm-pm8941.dtsi`: the 36 pm8941 pins (the vendor's configuration, now ours).
- `/home/user/Develop/src/android-air-gapped/standalone/vendor/android_kernel_lge_hammerhead/drivers/media/platform/msm/camera_v2/sensor/csid/msm_csid.c`: the CSID driver (LUT writer, `CORE_CTRL` writes).
- `/home/user/Develop/src/android-air-gapped/standalone/vendor/android_kernel_lge_hammerhead/drivers/media/platform/msm/camera_v2/ispif/msm_ispif.c`: the ISP-IF routing (the single-non-zero-cid crash).
- `/home/user/Develop/src/android-air-gapped/standalone/kruxport/kruxport/`: the port's modules (`touch.py`, `buttons.py`, `pmu.py`, `auto_shutdown.py`, `flash.py`, `board.py`, `lcd_bcm.c` is one level up).
- `/home/user/Develop/src/android-air-gapped/standalone/kruxport/lcd_bcm.c`: the display module (font scale 3, the idle-commit thread).
- `/home/user/Develop/src/android-air-gapped/standalone/scripts/deploy-firmware.sh`: pushes the firmware; `standalone/scripts/boot/boot-our-firmware.sh` recovers the device.
- `/home/user/Develop/src/android-air-gapped/build/build-kernel.sh`: the kernel build (`TOOLCHAIN=arch TOOLCHAIN_PATH=…`).
- `/home/user/Develop/src/android-air-gapped/standalone/BUILD-GUIDE.md`, `DIRECTORIES.md`, `standalone/RUNTIME-DATA.md`: the new documents.
- `/home/user/Develop/src/android-air-gapped/airgap-standalone.log.md`: the session log (entries 86–88).

## Important Context
- Device `0879524ab0068abca` (the real id is `0879524b0068abca`); adb host = the only channel.
- Boot image in use: `/tmp/opencode/boot-firmware.img` (our kernel + the stub ramdisk); the Android one is `/tmp/opencode/boot-android-cam.img`.
- Build cache lives in `/tmp/opencode/{ko-arch2,mp-build,armel-sysroot}` (symlinked to `/home/user/Develop/build-cache`); the toolchain is `/home/user/Develop/toolchains/arch-16.2.0`; `/tmp` is ~20 % used.
- The kernel image to flash is **`zImage-dtb`**, not `zImage` (the bootloader finds the DTB by scanning the kernel; a plain `zImage` gives "dtb not found").
- The CSID's LUT word at `0x0c` holds the data types of VC0..3: VC0=0x2b (10-bit image), VC1=0x12 (8-bit stream); `cfg0=0x23`, `cfg1=0x13`.
- Captured packet headers: `unmapped long 0x2b010405`, `mapped long 0x1201040a` — the image's packets are unmapped.
- The vendor's photo under the ROM kernel: `/sdcard/DCIM/Camera/IMG_20261021_051523.jpg` (2448×3264, 1.1 MB) — pulled once to `/tmp/opencode/vendor-photo.jpg`.
- Vendor's camera under our kernel, unattended: `monkey -p com.google.android.GoogleCamera -c android.intent.category.LAUNCHER 1`; the volume key is **zoom**, not the shutter; `keyevent 27` is the camera key.
- The CSID's state is printed when the camera is **closed** (`am force-stop`).
- `os` in MicroPython has no `os.path`; `%` with adjacent string literals misbehaves — use one literal.
- The camera's CSID/PHY state is dumped by the kernel's debug instrumentation on the `airgap/debug-capture` branch (the current branch is `airgap/pmic-keys`; the camera prints are in the tree).
- The device's `/data` is ~90 % full (3.0 GB free); `/data/local/tmp` is cleaned by the ROM's Android.
- The init stub writes the backlight to `max_brightness/2` (127) and the kernel's fb blanking later leaves it at 32.

## 2026-10-22 00:01:09 UTC - session compaction (auto)

* session: `ses_ec9cfaf7efferMLQZnvjWxHDO8`

## Objective
- Make the LG Nexus 5 (hammerhead) run Krux firmware standalone on our own kernel: working touch, keys, camera, and a slim reproducible project.
- Current focus: get **actual images** out of the camera (a non-empty frame/file), then make the firmware's camera page work.

## Requirements
- Air-gapped; no modem/Wi-Fi/BT, no privileged/signature permissions, revertible by reflash.
- One device, two threads: camera needs fastboot/TWRP + our kernel; MicroPython work runs on the ROM's Android. Camera boot+run cycle is atomic.
- No serial console → adb is the only debugging channel. No own init: reuse the TWRP ramdisk.
- Minimize changes to the Krux project so they can merge upstream; prefer the MaixPy API surface over firmware edits.
- Runtime design: hide our firmware "under" TWRP; firmware is the default payload; holding Vol+ gives TWRP; adb must start before our stub.
- Update `airgap-standalone.log.md` after each conclusion/report or change.
- `\7` means the ASCII BEL character (0x07) placed **in the chat text before** the command that needs the user's action.
- Keep command timeouts short; avoid `sed 's/^/  /'` (use `awk '{print "  " $0}'`).
- **"Your commands use too long delays. The device reboots much faster. Save time by reducing the wait"** → wait loops are now 2 s steps.
- Builds must use **ccache** (`CROSS_COMPILE="ccache arm-none-eabi-"`), build trees live under `/tmp` (~10 % of /tmp is affordable); toolchain may stay on disk.
- Images are tested with `fastboot boot` unless a flash is explicitly wanted; flashing must stay reversible.
- **User's list:** (0) free /tmp space, relocate toolchain, drop stale kernel objects; (1) camera highest priority, unattended tests; (2) fonts 150–200 %; (3) wipe function must not brick; (4) from-scratch build + quick guide; (5) review `work/` and `tmp/`; (6) kernel tools → `standalone/tools`; (7) `patches/linux/` → `standalone`; (8) document each directory; (9) backlight off on blank, entropy from CPU, HardFloat ABI note; (10) Krux runtime data directory; (11) test HardFloat; (12) why no splash at start; (13) final report: why the DTB fix was necessary now but not before.
- User's ROM feedback: to boot the ROM's kernel it must be booted manually via fastboot; all boot/ROM images were relocated to `images/`.
- User: "try not to repeat the scenario when the device is unreachable by adb or fastboot"; "Please continue performing the tasks without stopping"; "Please continue. You last thinking was garbled".

## Decisions
- Touch/keys were broken by **our port**, not the kernel (`buttons.py` PRESSED/RELEASED inverted; volume keys needed `qcom,master-en`).
- Kernel is built with the vendor's gcc 16.2.0 from Arch (`.pkg.tar.zst` unpacked to `/home/user/Develop/toolchains/arch-16.2.0`).
- Camera fault is in our side, not the author's unpublished commits: config, DTBs and CSID/ISP-IF code compare identical against the author's kernel.
- **The "ISP-IF cid-1 crash" was our own debug dump reading MISR registers (≥0x290), which hangs the AHB** → watchdog reset with no message. Not a NULL deref.
- **A panic used to power the phone off** (LGE panic magic + `pm8xxx_reset_pwr_off(1)`); now a panic restarts instead, so crashes self-recover and `/proc/last_kmsg` holds the trace.
- **The idle power-off was our port's `auto_shutdown.py`** calling `power_manager.shutdown()`; on K210 that is a sleep, so the port now blanks the backlight and stays alive.
- **The camera's CPP firmware was never found** because `request_firmware()` delegates to `ueventd`, whose firmware region the hammerhead lacks → the CPP driver now reads `/lib/firmware` itself.
- **Our replayed sensor sequence wrote all 14 captured arrays including other modes** → sensor landed in a wrong mode; the correct order is arrays 0–6 plus stream-on (8).
- The CSID v3.0 LUT has one word per VC (0x10/0x14/0x18/0x1c) but `msm_csid_cid_lut()` computes it the v2.0 way; the author uses `(cid>>2)*4+0x20` for the CID config where ours uses `cid*4+0x20` — the one real divergence, still unresolved.
- HardFloat is safe (`AT_HWCAP_FP` set); earlier "no VFP" note was wrong.

## Work State
### Completed
- Touch works; both volume keys work (pm8941 `master-en`, vendor's 36-pin DTS committed `acb586400b5`).
- ION heap masks fixed; CPP NULL-firmware oops fixed; CPP firmware 1.2.0 loads (chromatrix accepted).
- Fonts 150 %; splash idle-commit thread; backlight mapping 7–15→0–8; wipe made safe via port `flash.py`.
- Project slimming: 3.1 GB toolchains removed; `standalone/rootfs/stock/` cut 992 MB → 73 MB; docs `BUILD-GUIDE.md`, `DIRECTORIES.md`, `RUNTIME-DATA.md`.
- **Phone now boots into our firmware by itself**: our image flashed to the boot partition; `fastboot reboot` → our kernel + stub + firmware + adb. Reversible with `fastboot flash boot images/boot-rom-20261011.img`.
- **Panic → restart verified** (device came back on its own, `Last boot reason: reboot`), `/proc/last_kmsg` readable.
- Firmware no longer powers off when idle (port `auto_shutdown.py`).
- ISP-IF dump capped at 0x28c → "cid-1 crash" gone.
- **CPP firmware loads from our ramdisk**: `CPP FW Version 0x10020003`, no more "Invalid frame message", **a frame arrives** (`GOT A FRAME: index=2 bytesused=0`) under our own firmware.
- Sensor mode fixed: read-back now 3280×2464 matching the CAMIF.
- Author's kernel compared: identical DTBs (20260926 vs 20261011), identical camera kconfig, identical CSID/ISP-IF code paths; `CONFIG_HZ` differs (100 vs 300) plus radio options.
- New tools: `compare-roms.sh`, `diff-kernels.py`, `cmp-func.py`, `cmp-callseq.py`, `cmp-iow.py`, `crash-vc1.sh`, `sweep-vc.sh`.
- Commits on `airgap/pmic-keys`: `a6e0ecbc411` (panic→restart), `3e2e0156955` (ispif MISR), `fe3a12a2289` (cpp reads firmware itself). Camera debug prints in `msm_isp40.c`/`msm_isp_util.c`/`camera.c` and `nfc/bcm2079x.c` remain uncommitted.
- Log updated (~11 903 lines).

### Active
- **Getting a real image.** The frame is still empty (`bytesused=0`, "Invalid frame message" gone). Just booted the author's TWRP (`3.4.113-…-gda91d066bc6`, adb up as root) to run **our imaging server `imaging9` under his kernel** as the decisive control.
- Mapping experiment done: LUT cid1=0x2b/cid0=0x12 applied (`vc0 0x00002b12`, cfg0 0x13 cfg1 0x23) but packets dropped to 164/492 and still `unmapped long 0x00000000` — sensor barely streaming after the mode fix.
- VC sweep (`sweep-vc.sh`, poke register/value) prepared but only 0x3302/0x0383/0x0387 tried, all identical to baseline.

### Blocked
- ROM kernel source for `ge11370a8eb99`/`ge4a…`/`da91d066bc6` unpublished — comparison is binary-only.
- ROM's Android has no root/`/dev/mem`, so CSID registers can't be read while the ROM's working camera runs.

## Next Move
1. Push `imaging9` + the CPP firmware to `/data/local/tmp` on the author's TWRP kernel and run the same command; compare `GOT A FRAME`/`bytesused` and `CSID0 irq … unmapped long` against our kernel.
2. Determine the actual virtual channel of the image packets from the CSID's captured header, then align the LUT/sensor VC (poke candidates `0x3302 0x0383 0x0387 0x4108 0x0401 0x030c 0x0101 0x0301 0x0303 0x0305 0x0309 0x030b`).
3. Fix the CSID LUT/CID-config addressing divergence (`(cid>>2)*4+0x20` vs `cid*4+0x20`) if the register layout is confirmed wrong.
4. Save a non-empty frame to a file and verify pixels; then implement `sensor.set_pixformat` etc. in the port so the firmware's camera page works (`Camera not found: module 'sensor' has no attribute 'set_pixformat'`).
5. Re-check remaining user list items: splash verification, runtime-data dir, entropy path, HardFloat test, final report (incl. the DTB-fix question).

## Relevant Files
- `/home/user/Develop/src/android-air-gapped/standalone/cam/STATE.md`: camera state and history on one page.
- `/home/user/Develop/src/android-air-gapped/standalone/cam/imaging8.c`: imaging server (arg order documented in `main`; `vendor_sequence_order` fix; `poke_reg`/`poke_val`; `two_cids`).
- `/home/user/Develop/src/android-air-gapped/standalone/cam/sensor_config.h` + `standalone/cam/tools/gen-sensor-config.py`: captured vendor sensor arrays.
- `/home/user/Develop/src/android-air-gapped/standalone/scripts/cam/sweep-vc.sh`, `crash-vc1.sh`: VC sweep and crash repro.
- `/home/user/Develop/src/android-air-gapped/standalone/scripts/build/build-imaging.sh`: builds `imaging9` (`arm-linux-gnueabihf-gcc -O2 -static -march=armv7-a -mfpu=neon-vfpv3 -mfloat-abi=hard`).
- `/home/user/Develop/src/android-air-gapped/standalone/vendor/android_kernel_lge_hammerhead/drivers/media/platform/msm/camera_v2/sensor/csid/msm_csid.c`: LUT writer (`(cid>>2)*4`) and debug dump.
- `/home/user/Develop/src/android-air-gapped/standalone/vendor/android_kernel_lge_hammerhead/drivers/media/platform/msm/camera_v2/pproc/cpp/msm_cpp.c`: `cpp_read_fw_file`, `CPP_FW_MAX_SIZE`, firmware load.
- `/home/user/Develop/src/android-air-gapped/standalone/vendor/android_kernel_lge_hammerhead/arch/arm/mach-msm/restart.c`: panic→restart patch.
- `/home/user/Develop/src/android-air-gapped/standalone/rootfs/build-stub-ramdisk.sh`: puts CPP fw in `/lib/firmware`; needs `--force` to rebuild.
- `/home/user/Develop/src/android-air-gapped/standalone/rootfs/make-boot-image.sh`: packs image from `stock/boot-ourkern-clean-twrp.img` + `zImage-dtb`.
- `/home/user/Develop/src/android-air-gapped/standalone/scripts/kernel/`: `compare-roms.sh`, `diff-kernels.py`, `cmp-callseq.py`, `cmp-iow.py`.
- `/home/user/Develop/src/android-air-gapped/standalone/kruxport/kruxport/auto_shutdown.py`, `sensor.py`: idle-sleep fix; camera stub still to implement.
- `/home/user/Develop/src/android-air-gapped/standalone/scripts/deploy-firmware.sh`: pushes firmware to `/data/krux` and restarts.
- `/home/user/Develop/src/android-air-gapped/images/`: `boot-rom-20260926.img`, `boot-rom-20261011.img`, `boot-kernelsu-20261016.img`, `twrp-3.7.0_9-HH.R.17-MADV_WIPEONFORK.img`.
- `/home/user/Develop/src/android-air-gapped/airgap-standalone.log.md`: session log.

## Important Context
- Device `0879524b0068abca`; adb host is the only channel; recovery adb state shows as `recovery`.
- The phone currently runs the **author's TWRP kernel** (`gda91d066bc6`) booted via `fastboot boot images/twrp-…img`, with root adb. Our kernel is still in the boot partition.
- Our build: `/tmp/opencode/ko-arch2` (O= dir), kernel `/tmp/opencode/ko-arch2/arch/arm/boot/zImage-dtb`, images `/tmp/opencode/boot-fw11.img` etc. Build must be `CROSS_COMPILE="ccache arm-none-eabi-"` with `PATH=/home/user/Develop/toolchains/arch-16.2.0/usr/bin`.
- Kernel image must be `zImage-dtb`, never plain `zImage`.
- Use `/proc/last_kmsg` for panic traces; panic now reboots, `Last boot reason: reboot`.
- Kernel `vsnprintf` does not support `%zu` (reads number as pointer → oops); use `%u` with a cast.
- ISP-IF registers: implemented 0x240–0x28c only; 0x290+ are MISR and hang the AHB on read.
- CSID v3.0: `LUT_VC_0`=0x10/`VC_1`=0x14/`VC_2`=0x18/`VC_3`=0x1c, `CID_n_CFG`=0x20, captured-hdr regs 0x6C etc.; LUT word 0x10 reads `0x0000122b` (VC0=0x2b image, VC1=0x12 stream), cfg0 0x23, cfg1 0x13.
- `imaging9` argument order: 1 w, 2 h, 3 ion mask, 4 front/rear, 5 front count, 6 cpp fw, 7 stream src, 8 binned, 9 nocfg, 10 nocycle, 11 vc, 12 dt, 13 cid, 14 csid, 15 frames, 16 isp vfe, 17 ispif vfe, 18 pix clk, 19 other dt, 20 lut cid0, 21 lut cid1, 22 poke reg, 23 poke val, 24 two_cids.
- Working baseline command: `./imaging9 3264 2448 0x44000000 rear 0 /data/local/tmp/cpp_firmware_v1_2_0.fw 0 0 0 0 0 0x2b 0 0 1 0 0 0 0x12 0 1 0xffffffff 0`.
- Vendor photo under the ROM kernel exists: `/sdcard/DCIM/Camera/IMG_20261021_051523.jpg` (2448×3264).
- `/data` ~90 % full; `/data/local/tmp` heavily populated with captures.

## 2026-10-22 05:40:19 UTC - session compaction (auto)

* session: `ses_ec9cfaf7efferMLQZnvjWxHDO8`

## Objective
- Get the LG Nexus 5 (hammerhead) camera working in the air-gapped Krux firmware: the camera stack must deliver real frames to the firmware's camera page (MaixPy `sensor` API), on our own kernel, without modem/Wi-Fi/BT and without privileged/signature permissions.

## Requirements
- Air-gapped: no modem/Wi-Fi/BT, no privileged/signature permissions, revertible by reflash.
- Minimize changes to the Krux project so they merge upstream; prefer the MaixPy API surface over firmware edits.
- No serial console; adb is the only debugging channel. No own init: reuse the TWRP ramdisk.
- One device, two threads: camera needs fastboot/TWRP + our kernel; MicroPython work runs on the ROM's Android.
- Builds use ccache (`CROSS_COMPILE="ccache arm-none-eabi-"`), build trees under `/tmp`, toolchain at `/home/user/Develop/toolchains/arch-16.2.0`.
- Kernel image must be `zImage-dtb`, never plain `zImage`.
- Update `airgap-standalone.log.md` after each conclusion/report or change.
- Use short timeouts and 2 s wait steps; avoid `sed 's/^/  /'` (use `awk '{print "  " $0}'`).
- Redirect bulky tool output to files and read it later (tunnelling through the agent is slow).
- Ask the user explicitly whenever their action is required (`\7` = BEL before the request).
- Image tests: `fastboot boot` unless a flash is explicitly wanted; flashing stays reversible.
- User prefers building from upstream source and disassembling binaries over guessing; wants deducible fixes since the ROM author refused to share information.

## Decisions
- The camera was never broken: the "no frame" results came from an adb-driven shutter that never fired. Reliable trigger: `am start -a android.media.action.STILL_IMAGE_CAMERA` + `input keyevent 27` (KEYCODE_CAMERA).
- Our tree is branch `airgap/pmic-keys` at `fe3a12a2289` = `466f069dcee` (upstream) + our patches; the ROM's 20261011 banner also says `g466f069dcee`, but the ROM's camera core is *newer* CAF (its debug paths list `msm_buf_mgr/msm_generic_buf_mgr.c`, absent from our tree).
- ROM kernels decompress with gzip (one stream each: 20261011 at 0x3c8d, 20260926/TWRP at 0x3c21); the log's "no tool for this compression" was wrong.
- `images/boot-rom-20260926.img` and `boot-rom-20261011.img` are now byte-identical in kernel+ramdisk (the reorganisation replaced the 20260926 one); the real 20260926 kernel lives in `images/boot-kernelsu-20260926.img` (root via `su` works there; 20261011's KernelSU is broken).
- Use `fastboot boot` for tests; boot partition currently holds `images/boot-kernelsu-20260926.img`.
- The imaging server's ISP-IF config must set `crop_enable = 1` (else `msm_ispif_enable_crop()` never runs, PIX0 enable bit 0x80 never set, no data ever arrives).
- Our kernel tree: `msm_sensor.c` capture patched to `notify_change(..., ATTR_MODE, 0666)` (kernel umask 006 stripped the read bit); the capture fires in our own firmware but not under the ROM's Android (SELinux denies the kernel's file creation in /data/local/tmp).
- The sensor's configuration in our replay is wrong: vendor live state has PLL `0x0202/0x0203 = 0x18/0x12` (photo mode) vs our `0x09/0xc4`, plus `0x0304=0x0b`, `0x0306=0x02`, `0x0407=0x10`, geometry bytes swapped (`0x0340=0x09,0x0341=0xd6,0x0342=0x0d,0x0343=0x70`), and registers absent from the old capture (arrays 0–6,8 come from the older LGE HAL).

## Work State
### Completed
- Verified the camera works under our kernel (pristine `466f069dcee` + our config) and under our patched kernel: photos `IMG_20261021_210952.jpg` (874,030 B), `IMG_20261021_211000.jpg` (867,800 B), `IMG_20261021_211605.jpg` (865,569 B); images pulled and confirmed real.
- Positive control on the ROM's 20261011 kernel: `IMG_20261021_210628.jpg` (874,539 B) with the hand-pressed shutter.
- ROM kernel configs pulled from `/proc/config.gz`: `/tmp/opencode/cfg-20261011.txt` (1241 options), `cfg-20260926.txt` (1242). Only 68 lines differ from ours (radio/HID/modem + `CONFIG_GPIO_QPNP_PIN_DEBUG`).
- ROM kernels decompressed: `/tmp/opencode/rom20261011.raw`, `rom20260926real.raw`, `twrp.raw`; author's symbol table in `author-kallsyms.txt`.
- CSID table in our path now matches the vendor's exactly (`vc0 0x0000122b, cfg0 0x00000023, cfg1 0x00000013`).
- Tools built: `standalone/cam/regs.c` (CSID/ISP-IF/CAMIF via /dev/mem), `standalone/cam/sensordump.c` (reads the sensor's registers while the ROM camera app holds it; ioctl `_IOWR('V', 193, struct sensorb_cfg_data)`, pointer semantics `*(void **)c.cfg = &rc`), `standalone/scripts/cam/shoot.sh`, `standalone/scripts/cam/camtest.sh` (now uses keyevent 27).
- Log updated through "the camera was never broken" plus the CSID/clock findings.

### Active
- Make the imaging server's V4L2 path deliver pixels: program the sensor from the vendor's **live** register values (photo-mode list captured in `/tmp/opencode/hal-photo.txt`) instead of the old captured arrays 0–6,8, then re-test frame delivery (`bytesused`).
- Verify the resulting frame is non-empty and then wire the firmware's `sensor.snapshot()` to the imaging server.

### Blocked
- Capturing the newer HAL's *sequence* (not just its end state) via the kernel capture is blocked: under the ROM's Android the kernel's file creation in /data/local/tmp is denied by SELinux (works under our own firmware).
- `fastboot boot` silently fell back to the boot partition once; always check the `Booting OKAY` line.
- `su` is unavailable on the ROM's 20261011 kernel (no root); the ROM's Android denies `dmesg`/`/proc/kmsg` to the shell.

## Next Move
1. Add a "program the sensor from this explicit register list" mode to `standalone/cam/imaging8.c` (build `imaging11`), seeded with the vendor's live photo-mode values from `/tmp/opencode/hal-photo.txt` (0x0100=1 last).
2. Run it under our own firmware (`/tmp/opencode/boot-stub-camera.img` = stub ramdisk + current kernel), check `bytesused` in `[client] GOT A FRAME` and the CSID packet counter in `dmesg` (should be ~2.3 M packets, not 164).
3. If still empty, read more of the vendor's live registers (widen `sensordump.c`'s list, e.g. whole page 0) and re-program.
4. Then implement `snapshot()` in `standalone/kruxport/kruxport/sensor.py` (currently raises `SensorError`) against the working path, and test the Krux camera page on device.
5. Keep `airgap-standalone.log.md` current after each result.

## Relevant Files
- `standalone/cam/imaging8.c`: the imaging server (sensor sequence, CSID/PHY config, ISP-IF config, `crop_enable` fix, `vendor_sequence_order[] = {0,1,2,3,4,5,6,8}`).
- `standalone/cam/sensordump.c`: reads the IMX179's registers live (built to `sensordump`).
- `standalone/cam/regs.c`: reads CSID/ISP-IF/CAMIF registers via /dev/mem.
- `standalone/cam/sensor_config.h`: the old captured arrays (92/62/… registers) — the wrong, older-HAL sequence.
- `standalone/scripts/cam/shoot.sh`, `standalone/scripts/cam/camtest.sh`: the validated camera test flow.
- `standalone/kruxport/kruxport/sensor.py`: the MaixPy camera stand-in; `snapshot()` still raises.
- `standalone/vendor/android_kernel_lge_hammerhead/drivers/media/platform/msm/camera_v2/sensor/msm_sensor.c`: the capture code (patched with `notify_change`).
- `standalone/vendor/android_kernel_lge_hammerhead/drivers/media/platform/msm/camera_v2/sensor/csid/msm_csid.c`: `msm_csid_cid_lut()` (packed LUT, verified identical to the author's kernel).
- `airgap-standalone.log.md`: the session log (12,193+ lines).
- `/tmp/opencode/hal-photo.txt`, `hal-preview.txt`: vendor sensor state (photo/preview).
- `/tmp/opencode/cfg-20261011.txt`, `cfg-20260926.txt`: ROM kernel configs.
- `/tmp/opencode/rom20261011.raw`, `rom20260926real.raw`, `twrp.raw`: decompressed ROM kernels.
- `standalone/rootfs/make-boot-image.sh`, `standalone/rootfs/stock/`: stub-ramdisk boot images for our own firmware.

## Important Context
- Reliable camera trigger: `am start -a android.media.action.STILL_IMAGE_CAMERA`; `sleep 12`; `input keyevent 27`; `sleep 10`; `input keyevent 27`; `sleep 15`; check `/sdcard/DCIM/Camera/`.
- Build: `cd standalone/vendor/android_kernel_lge_hammerhead && PATH=/home/user/Develop/toolchains/arch-16.2.0/usr/bin:$PATH make ARCH=arm CROSS_COMPILE="ccache arm-none-eabi-" O=/tmp/opencode/ko-arch2 -j$(nproc)` (~45–80 s incremental).
- Userspace tools: `arm-linux-gnueabihf-gcc -O2 -static` (imaging server, sensordump, regs).
- Our own firmware boots from `/tmp/opencode/boot-stub-camera.img` (stub ramdisk + kernel); adbd is root there and `dmesg` works.
- Device `0879524b0068abca`; ROM's 20260926 KernelSU image gives `su -c id` → uid=0; the 20261011 image does not.
- Vendor sensor photo-mode values (live): 0x0100=1, 0x0202=0x18, 0x0203=0x12, 0x0204=0, 0x0205=0x34, 0x0301=5, 0x0302=0, 0x0303=1, 0x0304=0x0b, 0x0305=6, 0x0306=2, 0x0307=0xbc, 0x0308=0, 0x0309=5, 0x030a=0, 0x030b=1, 0x030c=0, 0x030d=0xca, 0x0340=0x09, 0x0341=0xd6, 0x0342=0x0d, 0x0343=0x70, 0x0344-0x0347=0, 0x0348=0x0c, 0x0349=0xcf, 0x034a=0x09, 0x034b=0x9f, 0x034c=0x0c, 0x034d=0xd0, 0x034e=0x09, 0x034f=0xa0, 0x0350=0, 0x0351=0, 0x0401=0, 0x0405=0x10, 0x0406=0, 0x0407=0x10, 0x098e=0, 0x0990=0, 0x3302=1, 0x3303=0, 0x33d4=0x0c, 0x33d5=0xd0, 0x33d6=0x09, 0x33d7=0xa0. Preview mode differs: 0x0203=0x9a, 0x0205=0xe0, 0x0340=0x18, 0x0341=0x9e.
- Our current failing symptom: CSID sees only 164 packets, `unmapped long 0x1201040a`, `mapped long 0x0`, `camif error status: 0xd0`, frame `bytesused=0`; the vendor's session reaches ~2.36 M packets.
- ISP-IF with a single non-zero cid crashes the kernel (it now restarts instead of powering off).
- Camera node: `/dev/v4l-subdev11` (sensor subdev), `/dev/video1` (sensor), `/dev/video0` (event node, busy while the Android camera service runs), `/dev/v4l-subdev13` (ISP), `/dev/v4l-subdev15` (ISP-IF).
- Kernel debug prints available under our own firmware: `DEBUG: CSID0 LUT:`, `DEBUG: CSID0 irq status:`, `DEBUG: ISP input:`, `camif violation`, `DEBUG: ISPIF reg …`.

## 2026-10-22 15:36:06 UTC - session compaction (auto)

* session: `ses_ec9cfaf7efferMLQZnvjWxHDO8`

## Objective
- Get the LG Nexus 5 (hammerhead) camera working in the air-gapped Krux firmware: the camera stack must deliver real frames to the firmware's camera page (MaixPy `sensor` API), on our own kernel, without modem/Wi-Fi/BT and without privileged/signature permissions.

## Requirements
- Air-gapped: no modem/Wi-Fi/BT, no privileged/signature permissions, revertible by reflash.
- Minimize changes to the Krux project so they merge upstream; prefer the MaixPy API surface over firmware edits.
- No serial console; adb is the only debugging channel. No own init: reuse the TWRP ramdisk.
- One device, two threads: camera needs fastboot/TWRP + our kernel; MicroPython work runs on the ROM's Android.
- Builds use ccache (`CROSS_COMPILE="ccache arm-none-eabi-"`), build trees under `/tmp`, toolchain at `/home/user/Develop/toolchains/arch-16.2.0`.
- Kernel image must be `zImage-dtb`, never plain `zImage`.
- Update `airgap-standalone.log.md` after each conclusion/report or change.
- Use short timeouts and 2 s wait steps; avoid `sed 's/^/  /'` (use `awk '{print "  " $0}'`).
- Redirect bulky tool output to files and read it later.
- Ask the user explicitly whenever their action is required (`\7` = BEL before the request).
- Image tests: `fastboot boot` unless a flash is explicitly wanted; flashing stays reversible.
- User prefers building from upstream source and disassembling binaries over guessing; wants deducible fixes since the ROM author refused to share information.
- **One photo per camera test**, not two: a second shutter press is a separate capture session and makes measurements ambiguous.
- **User correction (authoritative): our kernel is identical in code and config to the ROM's kernel** (except our fixes and minor config changes) — the earlier "our kernel has older CAF" framing is wrong and must not drive decisions.
- **User directive: stop working around the kernel crash and find/fix its cause** so data collection is steady; the user recalls an earlier case where a debug patch caused a crash by reading past the end of a data struct.

## Decisions
- The camera was never broken: the "no frame" results came from an adb-driven shutter that never fired. Reliable trigger: `am start -a android.media.action.STILL_IMAGE_CAMERA` + `input keyevent 27`.
- Our tree is branch `airgap/pmic-keys` at `fe3a12a2289` = `466f069dcee` (upstream) + our patches; ROM banner also says `g466f069dcee`; camera source/config are the same in both (per user; confirmed our tree has `msm_buf_mgr/msm_generic_buf_mgr.c`, same as the ROM's build paths).
- ROM kernels decompress with gzip (20261011 at 0x3c8d, 20260926/TWRP at 0x3c21).
- `images/boot-rom-20260926.img` and `boot-rom-20261011.img` are byte-identical in kernel+ramdisk; the real 20260926 kernel is in `images/boot-kernelsu-20260926.img` (root via `su` works there; 20261011's KernelSU is broken). Boot partition currently holds `images/boot-kernelsu-20260926.img`.
- KernelSU root does **not** work with our kernel (`su: inaccessible or not found`) — that avenue is dead; our own firmware (stub ramdisk + our kernel) is the debugging environment (uid 0, `dmesg` works).
- The imaging server's ISP-IF config must set `crop_enable = 1` (else `msm_ispif_enable_crop()` never runs and PIX0 enable bit is never set) — but the vendor sends `crop_enable = 0` and works, so crop is a knob, not a fact.
- The sensor's old captured arrays in `sensor_config.h` are the *preview* (older LGE HAL) sequence and are wrong; the vendor's **live photo-mode** values are the correct end state.
- **Vendor writes the geometry (`0x0340`, `0x0342`) as 16-bit words** (`data_type = 2`), everything else as bytes — proven from `sensorio_*.bin` ioctl headers. Byte writes reverse the geometry.
- Real 20260926/TWRP decompression, author's symbol table, and config diffs are settled: ROM configs pulled (`cfg-20261011.txt` 1241 options, `cfg-20260926.txt` 1242); only 68 lines differ from ours (radio/HID/modem + `CONFIG_GPIO_QPNP_PIN_DEBUG`).
- Kernel capture files work if the umask is lifted at creation (`current->fs->umask = 0` around `filp_open`), **not** by `notify_change` afterwards; need `#include <linux/fs_struct.h>`.
- `vfs_write()` copies from user memory, so kernel-side text dumps need a `kmap()`ed page (this 3.4 kernel: `kmap(pg)` with no `KM_USER`).
- `fastboot boot` silently falls back to the boot partition; always verify the `Booting OKAY` line **and** that `/proc/version` contains `gfe3a12a2289` and uid is 0 (adb state on our firmware is `recovery`, not `device`).

## Work State

### Completed
- Camera proven working under our kernel and our patched kernel: photos `IMG_20261021_210952.jpg` (874,030 B), `IMG_20261021_211000.jpg` (867,800 B), `IMG_20261021_211605.jpg` (865,569 B); positive control on the ROM's 20261011 kernel `IMG_20261021_210628.jpg` (874,539 B); under our kernel with our patches `IMG_20261021_223858.jpg` (882,796 B), `IMG_20261021_232820.jpg` (1,060,906 B), `IMG_20261021_232833.jpg` (1,199,234 B).
- ROM kernels decompressed to `/tmp/opencode/rom20261011.raw`, `rom20260926real.raw`, `twrp.raw`; author symbol table in `author-kallsyms.txt`; confirmed same camera files/generation as ours (both use `msm_buf_mgr/msm_generic_buf_mgr.c`).
- Sensor live state captured with `sensordump.c` (photo and preview modes) into `/tmp/opencode/hal-photo.txt`, `hal-preview.txt`; `sensor_live.h` now reproduces the vendor photo end state, read-back matches vendor exactly (`0x0340=0x09, 0x0341=0xd6, 0x0342=0x0d, 0x0343=0x70`, PLL `0x0202=0x18/0x0203=0x12`).
- Diagnostic confirmed the earlier capture blockage: kernel's file creation in `/data/local/tmp` is denied by SELinux under the ROM's Android (`avc: denied { create } ... scontext=u:r:kernel:s0`); `override_creds` does not change the SELinux context.
- Capture umask fix landed in `msm_sensor.c` and `msm_isp_util.c`; then the ROM's Android produced **6,848 ISP uploads + 13 sensor arrays** (all 0666), pulled as `/tmp/opencode/vcap/` (6,874 files) and `/tmp/opencode/cppcap/`.
- Vendor photo session analysed: ISP config is uploaded every frame; 1,141 cycles of 6 uploads; 1,120 byte-identical preview cycles; 21 unique = initial config + capture. Our original `cam_config.h` matched the vendor's 37 of 39 initial uploads in order.
- Generated from the vendor's capture: `cam_capture.h` (21 capture uploads, incl. `0x444=0x403f`, `0x464=0x30001f5`, `0x400=0x10000`, `0x638=0`), `cam_cycle.h` (capture's 6-upload per-frame cycle), and regenerated `cam_config.h` (56 initial uploads). Chromatrix (`cam_chromatrix.h`) verified byte-identical to the vendor's `cpp_cfg_0_len266.bin`.
- `imaging8.c` now: per-write data type (`sensor_write_regs_as`), separate `imx179_live_geometry[]` word array, capture-config and cycle-config replay, capture enable flag, stream source selector with correct summary naming, ISP started before sensor in `PRIV_STREAM_ON`, `CAMIF_RAW` stream source. Built as `imaging13`, `imaging14`, `imaging15` (599,048 B).
- Ordering bug fixed and verified: sensor stream-on now happens after `isp_start()`; with the live list the CSID receives 54k–69k packets instead of 164.
- `camtest.sh` reduced to one shutter press per run.
- `runcam.sh` helper written and debugged: boots our firmware, verifies kernel+root, pushes program/script, runs server in background, reads `dmesg` while alive. Works end-to-end.
- Log updated through the vendor photo-session capture and the geometry byte-order finding (now ~12,790+ lines).

### Active
- **Find and fix the cause of the kernel crash/reboot on camera close** (user's explicit direction), instead of racing to squeeze data out before it: suspected the `halt timeout` / ISP close path hang, and past precedent of a debug patch reading past a struct's end.
- Make data collection steady: CSID state (`csid_state.txt`), ISP-IF parameters (`ispif_state.txt`), ISP-IF ioctl trace (`ispif_ioctls.txt`) dumps exist in the kernel but are currently **created empty**; the `kmap()` fix was built but not yet verified to produce non-empty files.
- Determine why the ROM's camera HAL crashes under our kernel: `android.hardware.camera.provider-service.lineage` SIGSEGV (SEGV_MAPERR at `0xdac`, null pointer deref) in `qcamera::QCamera3HardwareInterface::initStaticMetadata(int)+1416` via `getCamInfo` — likely a struct/ABI mismatch, relevant to the same root cause family.
- Still to solve: the imaging server's V4L2 path yields `bytesused=0`, VFE `pix_count 0`, CSID packets 54k–69k (vendor reaches ~2.36 M), `camif error status: 0xd0`.

### Blocked
- Reading kernel-side state files under the ROM's Android (SELinux blocks `u:r:kernel:s0` file creation in `/data/local/tmp`), so vendor-session tracing must happen under our own firmware.
- `su`/KernelSU unavailable on the ROM's 20261011 kernel; that image also lacks a working root path with our kernel.
- `/dev/mem` absent (`CONFIG_DEVMEM` unset) in both kernels, so `regs.c` cannot be used for live register reads.
- ISP-IF with a single non-zero cid crashes the kernel.

## Next Move
1. Verify the `kmap()`-based writes now produce non-empty `/data/local/tmp/csid_state.txt`, `ispif_state.txt`, `ispif_ioctls.txt` after a run on our firmware (boot image already rebuilt, `boot-stub-camera.img` 22,689,792 B).
2. Diagnose the close-path crash: capture the full panic/oops (`dmesg` before reboot, `panic=`, `Call trace`, `msm_isp_close`, `halt timeout`) and check whether a debug dump reads past a struct (past precedent) or the ISP close genuinely hangs; fix it so runs end cleanly.
3. With a stable run, read the CSID/ISP-IF/VFE state for the failing case and compare against the vendor's working session (target ~2.36 M packets, non-zero `pix_count`).
4. Resolve the remaining block: why `bytesused=0` / `pix_count 0` while CSID receives 54k–69k packets (`camif error status: 0xd0`); consider ISP-IF `vfe_info` id/version (currently zeros), `crop_enable`, and the capture config/cycle now replaying verbatim.
5. Investigate the HAL SIGSEGV in `initStaticMetadata`/`getCamInfo` (struct/ABI mismatch) as a parallel clue to the same mismatch.
6. Then implement `snapshot()` in `standalone/kruxport/kruxport/sensor.py` (currently raises `SensorError`) and test the Krux camera page on device.
7. Keep `airgap-standalone.log.md` current after each result.

## Relevant Files
- `standalone/cam/imaging8.c`: the imaging server (live sensor list + word-data geometry, capture/cycle replay, ISP-IF config, `crop_enable`, stream source, ISP-before-sensor ordering).
- `standalone/cam/sensor_live.h`: vendor's live photo-mode register list; `imx179_live_geometry[]` word array.
- `standalone/cam/cam_capture.h`, `cam_cycle.h`, `cam_config.h`, `cam_chromatrix.h`: generated from the vendor's captured photo session.
- `standalone/cam/sensor_config.h`: old captured preview arrays (known wrong; kept for reference).
- `standalone/cam/tools/gen-cam-config.py`: generates the config headers from a capture directory.
- `standalone/cam/sensordump.c`, `standalone/cam/regs.c`: live sensor register reads; CSID/ISP-IF/CAMIF reads via `/dev/mem` (unusable: no `CONFIG_DEVMEM`).
- `standalone/scripts/cam/runcam.sh`: boots our firmware, verifies kernel/root, runs the server, reads `dmesg`.
- `standalone/scripts/cam/imaging.sh`: named settings for the server (argv: width height iova rear FW src … mode crop capture).
- `standalone/scripts/cam/camtest.sh`: validated one-photo camera test flow.
- `standalone/kruxport/kruxport/sensor.py`: MaixPy camera stand-in; `snapshot()` still raises.
- `standalone/vendor/android_kernel_lge_hammerhead/drivers/media/platform/msm/camera_v2/sensor/msm_sensor.c`: umask-lifted capture patches.
- `.../camera_v2/isp/msm_isp_util.c`: ISP config capture (umask fix).
- `.../camera_v2/sensor/csid/msm_csid.c`: CSID state dump (`csid_state.txt`, now via `kmap`).
- `.../camera_v2/ispif/msm_ispif.c`: ISP-IF parameter dump + ioctl trace (`ispif_state.txt`, `ispif_ioctls.txt`, now via `kmap`).
- `airgap-standalone.log.md`: session log (12,790+ lines).
- `/tmp/opencode/vcap/`, `/tmp/opencode/cppcap/`: vendor photo-session captures (6,874 ISP files; chromatrix).
- `/tmp/opencode/ko-arch2/`: kernel build tree; `/tmp/opencode/boot-stub-camera.img`: our firmware boot image; `/tmp/opencode/boot-android-branch.img`: ROM Android + our kernel.

## Important Context
- Device `0879524b0068abca`. On our firmware adb state is `recovery`, uid 0, `/proc/version` contains `3.4.113-🇺🇦-gfe3a12a2289-dirty`.
- Build: `cd standalone/vendor/android_kernel_lge_hammerhead && PATH=/home/user/Develop/toolchains/arch-16.2.0/usr/bin:$PATH make ARCH=arm CROSS_COMPILE="ccache arm-none-eabi-" O=/tmp/opencode/ko-arch2 -j$(nproc)` (~80 s incremental). Boot image: `sh standalone/rootfs/make-boot-image.sh standalone/rootfs/stock/boot-ourkern-clean-twrp.img /tmp/opencode/ko-arch2/arch/arm/boot/zImage-dtb /tmp/opencode/boot-stub-camera.img`.
- Userspace tools: `arm-linux-gnueabihf-gcc -O2 -static`.
- The camera's close path panics/reboots the device (previously logged as "halt timeout"); this is now the primary bug to fix, not a workaround target.
- `vfs_write()` from kernel memory silently writes 0 bytes; the sensor/ISP captures work because they write the HAL's user buffer.
- Vendor photo-mode values (live): `0x0100=1`, `0x0202=0x18`, `0x0203=0x12`, `0x0205=0x34`, `0x0304=0x0b`, `0x0306=0x02`, `0x0307=0xbc`, `0x030d=0xca`, `0x0340/41=0x09/0xd6`, `0x0342/43=0x0d/0x70`, `0x034c..0x034f=0x0c,0xd0,0x09,0xa0`, `0x0407=0x10`, `0x3302=0x01`, `0x33d4..0x33d7=0x0c,0xd0,0x09,0xa0`.
- Vendor capture-specific ISP regs: `0x400=0x10000`, `0x444=0x403f`, `0x464=0x30001f5`, `0x518=0x9380da`, `0x638=0`, `0x37c=1`; preview: `0x444=0x6020`, `0x464=0x18000fc`, `0x638=7`.
- Current failing symptom: CSID `irq status 0x000000dd`, ~54k–69k packets, mapped/unmapped long 0, VFE `stream 0: src 2, pix_count 0, raw_count 0`, frame `bytesused=0`, `camif error status: 0xd0`; LUT matches vendor (`vc0 0x0000122b`, `cfg0 0x23`, `cfg1 0x13`).
- Camera nodes: `/dev/v4l-subdev11` (sensor), `/dev/video1` (sensor), `/dev/video0` (event), `/dev/v4l-subdev13` (ISP), `/dev/v4l-subdev15` (ISP-IF).

## 2026-10-22 18:32:01 UTC - session compaction (manual)

* session: `ses_ec9cfaf7efferMLQZnvjWxHDO8`

## Objective
- Get the LG Nexus 5 (hammerhead) camera working in the air-gapped Krux firmware: the camera stack must deliver real frames to the firmware's camera page (MaixPy `sensor` API), on our own kernel, without modem/Wi-Fi/BT and without privileged/signature permissions.

## Requirements
- Air-gapped: no modem/Wi-Fi/BT, no privileged/signature permissions, revertible by reflash.
- Minimize changes to the Krux project so they merge upstream; prefer the MaixPy API surface over firmware edits.
- No serial console; adb is the only debugging channel. No own init: reuse the TWRP ramdisk.
- One device, two threads: camera needs fastboot/TWRP + our kernel; MicroPython work runs on the ROM's Android.
- Builds use ccache (`CROSS_COMPILE="ccache arm-none-eabi-"`), build trees under `/tmp`, toolchain at `/home/user/Develop/toolchains/arch-16.2.0`.
- Kernel image must be `zImage-dtb`, never plain `zImage`.
- Update `airgap-standalone.log.md` after each conclusion/report or change.
- Use short timeouts and 2 s wait steps; avoid `sed 's/^/  /'` (use `awk '{print "  " $0}'`).
- Redirect bulky tool output to files and read it later.
- Ask the user explicitly whenever their action is required (`\7` = BEL before the request).
- Image tests: `fastboot boot` unless a flash is explicitly wanted; flashing stays reversible.
- User prefers building from upstream source and disassembling binaries over guessing; wants deducible fixes since the ROM author refused to share information.
- **One photo per camera test**, not two: a second shutter press is a separate capture session and makes measurements ambiguous.
- **User correction (authoritative): our kernel is identical in code and config to the ROM's kernel** (except our fixes and minor config changes) — the earlier "our kernel has older CAF" framing is wrong and must not drive decisions.
- **User directive: stop working around the kernel crash and find/fix its cause** so data collection is steady; the user recalls an earlier case where a debug patch caused a crash by reading past the end of a data struct.

## Decisions
- The camera was never broken: the "no frame" results came from an adb-driven shutter that never fired. Reliable trigger: `am start -a android.media.action.STILL_IMAGE_CAMERA` + `input keyevent 27`.
- Our tree is branch `airgap/pmic-keys` at `fe3a12a2289` = `466f069dcee` (upstream) + our patches; ROM banner also says `g466f069dcee`; camera source/config are the same in both (per user; confirmed our tree has `msm_buf_mgr/msm_generic_buf_mgr.c`, same as the ROM's build paths).
- ROM kernels decompress with gzip (20261011 at 0x3c8d, 20260926/TWRP at 0x3c21).
- `images/boot-rom-20260926.img` and `boot-rom-20261011.img` are byte-identical in kernel+ramdisk; the real 20260926 kernel is in `images/boot-kernelsu-20260926.img` (root via `su` works there; 20261011's KernelSU is broken). Boot partition currently holds `images/boot-kernelsu-20260926.img`.
- KernelSU root does **not** work with our kernel (`su: inaccessible or not found`) — that avenue is dead; our own firmware (stub ramdisk + our kernel) is the debugging environment (uid 0, `dmesg` works).
- The imaging server's ISP-IF config must set `crop_enable = 1` (else `msm_ispif_enable_crop()` never runs and PIX0 enable bit is never set) — but the vendor sends `crop_enable = 0` and works, so crop is a knob, not a fact.
- The sensor's old captured arrays in `sensor_config.h` are the *preview* (older LGE HAL) sequence and are wrong; the vendor's **live photo-mode** values are the correct end state.
- **Vendor writes the geometry (`0x0340`, `0x0342`) as 16-bit words** (`data_type = 2`), everything else as bytes — proven from `sensorio_*.bin` ioctl headers. Byte writes reverse the geometry.
- Real 20260926/TWRP decompression, author's symbol table, and config diffs are settled: ROM configs pulled (`cfg-20261011.txt` 1241 options, `cfg-20260926.txt` 1242); only 68 lines differ from ours (radio/HID/modem + `CONFIG_GPIO_QPNP_PIN_DEBUG`).
- Kernel capture files work if the umask is lifted at creation (`current->fs->umask = 0` around `filp_open`), **not** by `notify_change` afterwards; need `#include <linux/fs_struct.h>`.
- `vfs_write()` copies from user memory, so kernel-side text dumps need a `kmap()`ed page (this 3.4 kernel: `kmap(pg)` with no `KM_USER`).
- `fastboot boot` silently falls back to the boot partition; always verify the `Booting OKAY` line **and** that `/proc/version` contains `gfe3a12a2289` and uid is 0 (adb state on our firmware is `recovery`, not `device`).
- **The kernel crash was in our own debug code**: `msm_csid_release()` read the CSID's captured-packet registers (0x6c/0x70/0x74), which are a diagnostic FIFO — reading one when empty hangs the AHB, the CPU stops, the watchdog resets the phone. Fixed by reading only plain counter registers (irq status, total packets, ECC, CRC) and writing the file with `set_fs(KERNEL_DS)`.

## Work State

### Completed
- Camera proven working under our kernel and our patched kernel: photos `IMG_20261021_210952.jpg` (874,030 B), `IMG_20261021_211000.jpg` (867,800 B), `IMG_20261021_211605.jpg` (865,569 B); positive control on the ROM's 20261011 kernel `IMG_20261021_210628.jpg` (874,539 B); under our kernel with our patches `IMG_20261021_223858.jpg` (882,796 B), `IMG_20261021_232820.jpg` (1,060,906 B), `IMG_20261021_232833.jpg` (1,199,234 B).
- ROM kernels decompressed to `/tmp/opencode/rom20261011.raw`, `rom20260926real.raw`, `twrp.raw`; author symbol table in `author-kallsyms.txt`; confirmed same camera files/generation as ours (both use `msm_buf_mgr/msm_generic_buf_mgr.c`).
- Sensor live state captured with `sensordump.c` (photo and preview modes) into `/tmp/opencode/hal-photo.txt`, `hal-preview.txt`; `sensor_live.h` now reproduces the vendor photo end state, read-back matches vendor exactly (`0x0340=0x09, 0x0341=0xd6, 0x0342=0x0d, 0x0343=0x70`, PLL `0x0202=0x18/0x0203=0x12`).
- Diagnostic confirmed the earlier capture blockage: kernel's file creation in `/data/local/tmp` is denied by SELinux under the ROM's Android (`avc: denied { create } ... scontext=u:r:kernel:s0`); `override_creds` does not change the SELinux context.
- Capture umask fix landed in `msm_sensor.c` and `msm_isp_util.c`; then the ROM's Android produced **6,848 ISP uploads + 13 sensor arrays** (all 0666), pulled as `/tmp/opencode/vcap/` (6,874 files) and `/tmp/opencode/cppcap/`.
- Vendor photo session analysed: ISP config is uploaded every frame; 1,141 cycles of 6 uploads; 1,120 byte-identical preview cycles; 21 unique = initial config + capture. Our original `cam_config.h` matched the vendor's 37 of 39 initial uploads in order.
- Generated from the vendor's capture: `cam_capture.h` (21 capture uploads, incl. `0x444=0x403f`, `0x464=0x30001f5`, `0x400=0x10000`, `0x638=0`), `cam_cycle.h` (capture's 6-upload per-frame cycle), and regenerated `cam_config.h` (56 initial uploads). Chromatrix (`cam_chromatrix.h`) verified byte-identical to the vendor's `cpp_cfg_0_len266.bin`.
- `imaging8.c` now: per-write data type (`sensor_write_regs_as`), separate `imx179_live_geometry[]` word array, capture-config and cycle-config replay, capture enable flag, stream source selector with correct summary naming, ISP started before sensor in `PRIV_STREAM_ON`, `CAMIF_RAW` stream source. Built as `imaging13`, `imaging14`, `imaging15` (599,048 B).
- Ordering bug fixed and verified: sensor stream-on now happens after `isp_start()`; with the live list the CSID receives 54k–69k packets instead of 164.
- `camtest.sh` reduced to one shutter press per run.
- `runcam.sh` helper written and debugged: boots our firmware, verifies kernel+root, pushes program/script, runs server in background, reads `dmesg` while alive. Works end-to-end.
- **Kernel crash fixed**: the CSID close path no longer reads the FIFO registers; close completes cleanly and the device survives repeated camera runs. The CSID state file is now written correctly (57 bytes, mode 0666, uid 0).
- Log updated through the crash fix (now ~13,000+ lines).

### Active
- **Determine why the V4L2 path yields `bytesused=0` / `pix_count=0`** while CSID receives 54k–69k packets (vendor reaches ~2.36 M), `camif error status: 0xd0`, VFE `stream 0: src 2, pix_count 0, raw_count 0`.
- Debug the ISP buffer pipeline: `msm_isp_process_done_buf` / `msm_isp_buf_done` instrumentation added (build b25 pending) to trace whether buffers are completed and with what timestamps.
- Compare our ISP-IF/CSID/CAMIF/VFE configuration against the vendor's captured photo session (initial config + per-frame cycle + capture config).

### Blocked
- Reading kernel-side state files under the ROM's Android (SELinux blocks `u:r:kernel:s0` file creation in `/data/local/tmp`), so vendor-session tracing must happen under our own firmware.
- `su`/KernelSU unavailable on the ROM's 20261011 kernel; that image also lacks a working root path with our kernel.
- `/dev/mem` absent (`CONFIG_DEVMEM` unset) in both kernels, so `regs.c` cannot be used for live register reads.
- ISP-IF with a single non-zero cid crashes the kernel.

## Next Move
1. Build and boot kernel b25 (buf_done instrumentation), run the imaging server, capture `dmesg` to see whether `msm_isp_process_done_buf` is called and `buf_done` fires.
2. If buffers complete but `bytesused=0`, trace why the VFE pixel path doesn't fill them: check CAMIF geometry (`0x2f4`, `0x2e8`, `0x300`), VFE input source (`src 2 = CAMIF_RAW`), and ISP stream configuration (`msm_isp_request_axi_stream`).
3. Compare our ISP register uploads (initial + cycle + capture) against the vendor's 21 unique capture uploads — ensure `0x400=0x10000`, `0x444=0x403f`, `0x464=0x30001f5`, `0x638=0`, `0x518=0x9380da`, `0x37c=1` are all sent.
4. Check CSID LUT matches vendor (`vc0 0x0000122b`, `cfg0 0x23`, `cfg1 0x13`) and that the routed CID is mapped to PIX0.
5. Once frames arrive, implement `snapshot()` in `standalone/kruxport/kruxport/sensor.py` (currently raises `SensorError`) and test the Krux camera page on device.
6. Keep `airgap-standalone.log.md` current after each result.

## Relevant Files
- `standalone/cam/imaging8.c`: the imaging server (live sensor list + word-data geometry, capture/cycle replay, ISP-IF config, `crop_enable`, stream source, ISP-before-sensor ordering).
- `standalone/cam/sensor_live.h`: vendor's live photo-mode register list; `imx179_live_geometry[]` word array.
- `standalone/cam/cam_capture.h`, `cam_cycle.h`, `cam_config.h`, `cam_chromatrix.h`: generated from the vendor's captured photo session.
- `standalone/cam/sensor_config.h`: old captured preview arrays (known wrong; kept for reference).
- `standalone/cam/tools/gen-cam-config.py`: generates the config headers from a capture directory.
- `standalone/cam/sensordump.c`, `standalone/cam/regs.c`: live sensor register reads; CSID/ISP-IF/CAMIF reads via `/dev/mem` (unusable: no `CONFIG_DEVMEM`).
- `standalone/scripts/cam/runcam.sh`: boots our firmware, verifies kernel/root, runs the server, reads `dmesg`.
- `standalone/scripts/cam/imaging.sh`: named settings for the server (argv: width height iova rear FW src … mode crop capture).
- `standalone/scripts/cam/camtest.sh`: validated one-photo camera test flow.
- `standalone/kruxport/kruxport/sensor.py`: MaixPy camera stand-in; `snapshot()` still raises.
- `standalone/vendor/android_kernel_lge_hammerhead/drivers/media/platform/msm/camera_v2/sensor/msm_sensor.c`: umask-lifted capture patches.
- `.../camera_v2/isp/msm_isp_util.c`: ISP config capture (umask fix).
- `.../camera_v2/sensor/csid/msm_csid.c`: CSID state dump (`csid_state.txt`) — fixed to read only counters, write with `set_fs(KERNEL_DS)`.
- `.../camera_v2/ispif/msm_ispif.c`: ISP-IF parameter dump + ioctl trace (`ispif_state.txt`, `ispif_ioctls.txt`).
- `.../camera_v2/isp/msm_isp_axi_util.c`: `msm_isp_process_done_buf` / `msm_isp_eof_notify` instrumentation.
- `.../camera_v2/isp/msm_buf_mgr.c`: `msm_isp_buf_done` instrumentation.
- `airgap-standalone.log.md`: session log (13,000+ lines).
- `/tmp/opencode/vcap/`, `/tmp/opencode/cppcap/`: vendor photo-session captures (6,874 ISP files; chromatrix).
- `/tmp/opencode/ko-arch2/`: kernel build tree; `/tmp/opencode/boot-stub-camera.img`: our firmware boot image; `/tmp/opencode/boot-android-branch.img`: ROM Android + our kernel.

## Important Context
- Device `0879524b0068abca`. On our firmware adb state is `recovery`, uid 0, `/proc/version` contains `3.4.113-🇺🇦-gfe3a12a2289-dirty`.
- Build: `cd standalone/vendor/android_kernel_lge_hammerhead && PATH=/home/user/Develop/toolchains/arch-16.2.0/usr/bin:$PATH make ARCH=arm CROSS_COMPILE="ccache arm-none-eabi-" O=/tmp/opencode/ko-arch2 -j$(nproc)` (~80 s incremental). Boot image: `sh standalone/rootfs/make-boot-image.sh standalone/rootfs/stock/boot-ourkern-clean-twrp.img /tmp/opencode/ko-arch2/arch/arm/boot/zImage-dtb /tmp/opencode/boot-stub-camera.img`.
- Userspace tools: `arm-linux-gnueabihf-gcc -O2 -static`.
- The camera's close path used to panic/reboot the device — fixed by removing FIFO register reads in `msm_csid_release()`.
- `vfs_write()` from kernel memory silently writes 0 bytes without `set_fs(KERNEL_DS)`; the sensor/ISP captures work because they write the HAL's user buffer.
- Vendor photo-mode values (live): `0x0100=1`, `0x0202=0x18`, `0x0203=0x12`, `0x0205=0x34`, `0x0304=0x0b`, `0x0306=0x02`, `0x0307=0xbc`, `0x030d=0xca`, `0x0340/41=0x09/0xd6`, `0x0342/43=0x0d/0x70`, `0x034c..0x034f=0x0c,0xd0,0x09,0xa0`, `0x0407=0x10`, `0x3302=0x01`, `0x33d4..0x33d7=0x0c,0xd0,0x09,0xa0`.
- Vendor capture-specific ISP regs: `0x400=0x10000`, `0x444=0x403f`, `0x464=0x30001f5`, `0x518=0x9380da`, `0x638=0`, `0x37c=1`; preview: `0x444=0x6020`, `0x464=0x18000fc`, `0x638=7`.
- Current failing symptom: CSID `irq status 0x000000dd`, ~54k–69k packets, mapped/unmapped long 0, VFE `stream 0: src 2, pix_count 0, raw_count 0`, frame `bytesused=0`, `camif error status: 0xd0`; LUT matches vendor (`vc0 0x0000122b`, `cfg0 0x23`, `cfg1 0x13`).
- Camera nodes: `/dev/v4l-subdev11` (sensor), `/dev/video1` (sensor), `/dev/video0` (event), `/dev/v4l-subdev13` (ISP), `/dev/v4l-subdev15` (ISP-IF).
- Kernel b25 built with `buf_done` instrumentation — needs boot and test.

## 2026-10-28 00:03:24 UTC - session compaction (manual)

* session: `ses_ec9cfaf7efferMLQZnvjWxHDO8`

## Objective
- Krux firmware (MicroPython port) working on the LG Nexus 5 (hammerhead) air-gapped device; fix the reported device issues first, then the camera, on our own kernel.

## Requirements
- Core constraints unchanged: air-gapped (no modem/Wi-Fi/BT), no privileged or signature permissions, revertible by reflash, adb as the only debug channel.
- Minimize changes to upstream Krux; prefer port-level shims over firmware edits.
- Docs live in `docs/` with `status-`/`build-` prefixes; `reference/` and `archive/` keep plain names.
- Single `master` branch.
- Builds go to `/tmp/opencode`; the kernel image is `zImage-dtb`.
- Test with `fastboot boot`; never flash unless the user requests it.
- Only deducible fixes (build from source or disassemble); no guessing.
- Do not rewrite the history of `docs/standalone-log.md` or `docs/compaction-log.md`.
- Ask the user with BEL when a physical action is needed.

## Decisions
- Keep MicroPython (not MaixPy).
- The port lives in the `krux` submodule on branch `nexus-port`.
- The deploy script only pushes to the device.
- `tools/` holds build infrastructure; `scripts/` holds device probes.
- The `krux/firmware/MaixPy` submodule is intentionally left uninitialized.
- No binary libsecp256k1; the pure-Python secp256k1 fallback is used.
- The mbedtls-backed `crypto_fast` C module replaces the slow pure-Python sha512/PBKDF2.
- The camera was postponed until the UI/NFC/crypto issues were fixed.
- The history rewrite to drop committed build artifacts was not executed (destructive).

## Work State
### Completed
- Repo restructure: `standalone/` flattened, submodules set up.
- All three builds verified: kernel `zImage-dtb`, MicroPython arm32, stub ramdisk/boot image.
- Port committed on `nexus-port`.
- Device boots our firmware via `fastboot boot` (adb state `recovery`, uid 0).
- Fixed on device: About-screen QR (bytes return from `qrcode_maixpy.c`); crypto (`crypto_fast`: sha512 + pbkdf2_hmac ~24–26 ms for 2048 iterations); hmac (string digestmod, 128-byte sha512 blocks, `copy()`); Vol+/Vol− swap; brightness sysfs write; tamper-check sha512 import.
- MaixPy shims (`image`/`fpioa_manager`/`Maix`) added; scripts archived with a triage doc; NFC transport imports + `available()` + auto-connect; splash flush + 3 s hold attempted.
- New this session: mnemonic creation fixed (`hashlib.sha256` must stay a hash object; only sha512 needs the `_FastSHA512` wrapper; `_cf` import ordering fixed); the user confirmed "Mnemonic creation works now!".

### Active
- Device verification of the splash: still not visible — the menu blinks with ~2 s blank; the framebuffer renders, but `MSMFB_DISPLAY_COMMIT` + `FBIOPAN_DISPLAY` do not flip the panel. The user agreed to defer this.
- NFC/SeedKeeper with a real SeedKeeper JavaCard (JCOP4SID): discovery sometimes misses the card ("no card within 3000/30000 ms"); when discovery succeeds, the SeedKeeper SELECT APDU gets no answer ("card didn't respond"). NFC DEBUG logging was toggled during tests.

### Blocked
- Camera sensor API gap (`sensor` module lacks `set_pixformat` etc.).
- Kernel V4L2 camera backlog (`bytesused=0`/`pix_count=0`, CSID 54k–69k vs vendor 2.36M packets, camif error `0xd0`; b25 `buf_done` build untested).
- `/dev/mem` absent.
- SELinux blocks kernel state files under ROM Android.

## Next Move
1. Finish the NFC discovery/activation debug (trace discover NCI params, RF discover map, ISO-DEP activation, retry cadence) using the debug trace.
2. Commit the docs restructure/archival/port/misc changes.
3. Start the camera plan (`docs/cam-plan.md`).
4. Defer the splash.

## Relevant Files
- `kruxport/kruxport/nfc.py`
- `kruxport/kruxport/uhashlib_hw.py`
- `kruxport/kruxport/hmac.py`
- `kruxport/crypto_fast.c`
- `kruxport/apply-micropython-port.sh`
- `kruxport/build-micropython.sh`
- `scripts/deploy-firmware.sh`
- `tools/build-kernel.sh`
- `rootfs/build-stub-ramdisk.sh`
- `rootfs/make-boot-image.sh`
- `krux/src/krux/camera.py`
- `kruxport/kruxport/sensor.py`
- `docs/*`

## Important Context
- Project: `/home/user/Develop/src/krux-nexus`. Submodules: `krux` (`nexus-port`), `micropython` (master `a3af47edc`, 1.30.0-dev), `linux` (`airgap/master-cleanup` `95f4b0f38ce`).
- Device `0879524b0068abca`; deploy with `DEPLOY_RESTART=1 bash scripts/deploy-firmware.sh`; reboot via `adb reboot bootloader` + `fastboot boot /tmp/opencode/boot-stub-test.img`.
- MicroPython has no `traceback` module (use `sys.print_exception`); builtin `hashlib` rejects `setattr` (inject through `sys.modules`); mbedtls is already linked by the unix port (add only `crypto_fast.c`).

## 2026-10-28 01:12:24 UTC - session compaction (manual)

* session: `ses_ec9cfaf7efferMLQZnvjWxHDO8`

## Objective
- Get Krux firmware (MicroPython port) fully working on the LG Nexus 5 (hammerhead) air-gapped device: fix device issues, then the camera, on our own kernel.

## Requirements
- Air-gapped: no modem/Wi-Fi/BT; no privileged/signature permissions; revertible by reflash; adb is the only debug channel.
- Minimize changes to upstream Krux so they can merge upstream; prefer port-level shims over firmware edits.
- All documentation except basic README goes in `docs/`; archived docs in `docs/archive/`.
- Use file prefixes (`status-*`, `build-*`) for most docs; `reference/` and `archive/` keep plain names.
- Krux-Nexus has a single branch (`master`) with all files on it.
- Android-related work can be dropped; "recovery" means deploying MicroPython+Krux inside TWRP ramdisk.
- Builds must use `/tmp/opencode` for object/build outputs (tmpfs, avoids SSD wear).
- Kernel image must be `zImage-dtb`; `fastboot boot` for tests (no flash) unless explicitly requested.
- Prefer building from source and disassembly over guessing; deducible fixes only.
- Do not touch `docs/standalone-log.md` / `docs/compaction-log.md` contents except appending new compaction events.
- `docs/continue-prompt.md` to be refreshed later (deferred).
- Ask the user explicitly (prefix `\7`/BEL) whenever their physical action on the device is required.
- User invites objections with reasoning for any request.

## Decisions
- Keep **MicroPython**, not MaixPy (reasoning in `docs/standalone-log.md` Entry 12).
- Canonical port sources live in the submodules: Python in `krux/src/` (+ `krux/src/krux/`), C in `micropython/ports/unix/` (`krux` branch `nexus-port`, `micropython` `master`).
- `kruxport/` holds only build output (`micropython-arm32`, gitignored) + README; build scripts in `scripts/` (`scripts/build/`).
- `tools/` = reusable build/config helpers; `scripts/` = build drivers + device probes; dead scripts in `scripts/archive/<topic>/`.
- No binary libsecp256k1: `secp256k1.py` is embit's pure-Python fallback.
- `crypto_fast` C module (mbedtls) replaces slow pure-Python sha512/PBKDF2; `hashlib.sha256` stays native.
- Camera postponed until NFC/UI/crypto fixed; plan exists (`docs/cam-plan.md`).
- NFC discovery: do **not** send `RF_DEACTIVATE` + second routing/discover round (it turns RF field off and it never returns); rely on bring-up's single `RF_DISCOVER`, accept status `0x06` as "already active".

## Work State
### Completed
- Repo restructure (kernel/MicroPython/stub ramdisk builds verified); device boots our firmware via `fastboot boot /tmp/opencode/boot-stub-test.img`; adb state `recovery`, uid 0.
- Fixed: About QR, crypto (`crypto_fast` sha512/PBKDF2/HMAC), hmac shim, Vol+/Vol−, brightness, tamper check, mnemonic creation, NFC transport imports/auto-connect, MaixPy shims.
- Port C modules committed in `micropython` (`dc5042bab`); Krux port commits in `krux` (NFC discovery, tag notification, 2-byte OID-6).
- Layout consolidation: duplicates removed from `kruxport/`; `apply-micropython-port.sh` deleted; `test_nfc.py` → `scripts/nfc-test.py`; `build-micropython.sh` → `scripts/build/build-micropython.sh` (fixed `SRC`/`OUT`/sysroot paths); `build-airgap-unused-ports-minimal.sh` → `scripts/archive/misc/`; stale `scripts/build/build-mpy.sh` removed.
- Docs updated and committed: `cce78bc` (script moves/layout/READMEs, gitlinks), `235032b` (`docs/cam-plan.md`).
- NFC now works end-to-end through activation and APDUs:
  - Bring-up succeeds; discovery finds and activates the SeedKeeper (`RF_INTF_ACTIVATED_NTF` with card name `PVJCOP4SID`).
  - AID SELECT `00 A4 04 00 0A "SeedKeeper"` returns a 13-byte data payload + `9000`; `B0 3C 00 00` (GET_STATUS) also returns `9000` with the same 13 bytes.
  - VERIFY_PIN `B0 42 00 00 06 "123456"` fails: chip sends `CORE_INTERFACE_ERROR_NTF` (`10 60 08 02 b2 00`), status `0xB2` = `RF_TIMEOUT_EXCEPTION`; card then stops answering subsequent SELECTs.
  - Error shown by app: "communication failed: the card did not answer", after a few seconds, even without a fresh tap.
- `_data`/`_activate` patched on device: recognises `RF_DISCOVER_NTF` (OID 3), builds SELECT params from it, checks buffered notifications, 2 s per-APDU timeout.

### Active
- Diagnose/fix `RF_TIMEOUT_EXCEPTION` on the VERIFY_PIN APDU (card likely slow due to on-card PBKDF2; official Android app may extend the transceive timeout).
- Checked NFA code: `nfa_dm_set_total_duration()` (`nfa_dm_discover.c:409`, param `NFC_PMID_TOTAL_DURATION`) — general lead for discovery timing, not yet tied to ISO-DEP RF timeout.
- Need to determine how Android/libnfc-nci extends the ISO-DEP/RF timeout (`IsoDep.setTimeout`) and whether to replay it (parameter/config or host retry policy).

### Blocked
- Camera: MaixPy `sensor` API gap over V4L2; kernel-side camera backlog (`bytesused=0`/`pix_count=0`, CSID packet counts, `camif error status: 0xd0`, b25 buf_done untested).
- `/dev/mem` absent; SELinux blocks kernel-side state files under ROM Android.
- Splash: framebuffer renders but `MSMFB_DISPLAY_COMMIT` + `FBIOPAN_DISPLAY` do not flip panel (deferred).

## Next Move
1. In the HAL/stack sources, find how the NFCC's ISO-DEP RF timeout is configured/extended (Android `IsoDep.setTimeout` path); implement the equivalent in `krux/src/nfc.py` if it is an NCI config, or verify whether a longer host timeout suffices.
2. If no config path: test with a deliberately wrong PIN to see whether the card answers at all (expect `9C02`); compare with the ROM/vendor stack capturing the same VERIFY_PIN on our logging kernel.
3. Handle `CORE_INTERFACE_ERROR`/`0xB2` explicitly in `_data` (distinct error), and re-activate/re-discover after link loss instead of blindly retrying APDUs.
4. Commit `krux/src/nfc.py` changes (discovery/activation/link-error fixes) with a clear message.
5. After NFC: start camera Phase A per `docs/cam-plan.md`; refresh `docs/continue-prompt.md`; defer splash.

## Relevant Files
- `krux/src/nfc.py`: NFC transport; bring-up, discovery, activation, `_data`; `DEBUG=1`.
- `krux/src/krux/seedkeeper.py`: APDU protocol/status words (`INS_VERIFY_PIN=0x42`, `INS_GET_STATUS=0x3C`).
- `micropython/ports/unix/nfc_bcm2079x.c`: `_nfc` C module (open/power/wake/read/write).
- `phase0/libnfc-nci-20791b05.conf`: vendor `NFA_DM_START_UP_CFG` (matches our replayed config, incl. `B5:03:01:02:FF`).
- `nfc/halimpl/hal/nfc_hal_nci.c`, `nfc/halimpl/hal/nfc_hal_dm_cfg.c`: vendor HAL config/NCI message handling.
- `nfc/stack/nfa/dm/nfa_dm_discover.c`: NFA discovery, `nfa_dm_set_total_duration`, FWI config.
- `docs/cam-state.md`: captured vendor NFC bring-up/activation trace.
- `docs/nfc-isodep.md`, `docs/nfc-transport.md`: NFC design/status references.
- `docs/cam-plan.md`: camera completion plan.
- `docs/build-guide.md`, `docs/directories.md`, `docs/scripts-triage.md`, `scripts/README.md`, `kruxport/README.md`: updated layout/build docs.
- `scripts/build/build-micropython.sh`: firmware build (output `kruxport/micropython-arm32`).
- `scripts/deploy-firmware.sh`: pushes interpreter/src and restarts service.
- `/tmp/opencode/nfc_listen2.py`: passive NFC probe with discovery/activation/APDU logging.
- `/tmp/opencode/fw_app2.log`: latest app packet log (activation + APDU + `0xB2` failure).

## Important Context
- Project dir: `/home/user/Develop/src/krux-nexus`; submodules `krux` (`nexus-port`), `micropython` (`master`), `linux` (`airgap/master-cleanup`).
- Device `0879524b0068abca`; currently running our firmware; restart only via `adb reboot bootloader` + `fastboot boot /tmp/opencode/boot-stub-test.img`.
- Deploy: `DEPLOY_RESTART=1 bash scripts/deploy-firmware.sh`; firmware log `/data/krux/fw.log` (truncated on service start); `setprop ctl.stop/start krux`.
- Stopping the `krux` service blanks the screen (panel stays lit); restarting brings touch/panel back after ~10 s.
- Only one process can usefully use `/dev/bcm2079x`: a background listener and the app steal each other's packets. Kill the listener (`ps | grep micropython`, `kill <pid>`) before app tests.
- NCI notes: `0x68` = CORE notification; `10 60 06 03 01 00 01` = credits (normal, appears after every APDU); `10 60 08 02 b2 00` = `CORE_INTERFACE_ERROR_NTF` status `0xB2` = `RF_TIMEOUT_EXCEPTION`.
- `RF_FIELD_INFO_NTF` is `10 61 07 01 00` (field OFF); field never turned ON after the old deactivate+rediscover path.
- Vendor trace: tag activation `RX 10 61 06 02 01 00` → `TX 10 21 04 03 01 04 02` → `RX 10 41 04 01 00` → `RX 10 61 05 ...`; APDU out as `10 00 00 <len> <apdu>`, response `10 00 00 <len> <data><SW1><SW2>`.
- MicroPython: no `traceback` (use `sys.print_exception`); builtin `hashlib` rejects `setattr`; `raise ... from` prints "Warning: exception chaining not supported".
- Build firmware: `bash scripts/build/build-micropython.sh` (~3 min, `BUILD=/tmp/opencode/mp-build`).

* Follow-up after this compaction: the `0xB2` diagnosis was narrowed down the same day — the card's status says `needs_secure_channel = 1` and the applet rejects all plaintext instructions except GET_STATUS with `0x9C20`, so simple PIN verification cannot work on this stock card. The port now names the NCI interface error and reconnects after link loss. Details, sources and the open "silence instead of 0x9C20" question are in `docs/nfc-isodep.md` ("Status 2026-10-28").
## 2026-10-28 01:14:49 UTC - session compaction (auto)

* session: `ses_ec9cfaf7efferMLQZnvjWxHDO8`

## Objective
- Get Krux firmware (MicroPython port) fully working on the LG Nexus 5 (hammerhead) air-gapped device: fix device issues, then the camera, on our own kernel.

## Requirements
- Air-gapped: no modem/Wi-Fi/BT; no privileged/signature permissions; revertible by reflash; adb is the only debug channel.
- Minimize changes to upstream Krux so they can merge upstream; prefer port-level shims over firmware edits.
- All documentation except basic README goes in `docs/`; archived docs in `docs/archive/`; use `status-*`/`build-*` prefixes except in `reference/`/`archive/`.
- Single `master` branch with all files on it.
- Android-related work can be dropped; "recovery" means deploying MicroPython+Krux inside TWRP ramdisk.
- Builds must use `/tmp/opencode` for object/build outputs; kernel image must be `zImage-dtb`; `fastboot boot` for tests (no flash) unless explicitly requested.
- Prefer building from source/disassembly over guessing; deducible fixes only.
- Do not touch `docs/standalone-log.md` / `docs/compaction-log.md` except appending new compaction events.
- `docs/continue-prompt.md` refresh deferred.
- Ask the user explicitly (prefix `\7`/BEL) whenever physical action on the device is required.
- User invites objections with reasoning.

## Decisions
- Keep **MicroPython**, not MaixPy; canonical port sources in submodules (`krux/src/`, `micropython/ports/unix/`).
- `kruxport/` holds only build output; build scripts in `scripts/`; `tools/` = reusable helpers.
- No binary libsecp256k1: pure-Python `secp256k1.py` fallback.
- `crypto_fast` (mbedtls) replaces slow pure-Python sha512/PBKDF2/HMAC; `hashlib.sha256` stays native.
- Camera postponed until NFC/UI/crypto fixed.
- NFC discovery: do **not** send `RF_DEACTIVATE` + second routing/discover round; rely on bring-up's single `RF_DISCOVER`, accept status `0x06` as "already active".
- Android `IsoDep.setTimeout` is host-side only; it cannot override the NFCC's `RF_TIMEOUT_EXCEPTION`, so a longer `nfc.py` timeout is not the fix.
- Stock SeedKeeper cards require the secure channel; Krux's plaintext-only `seedkeeper.py` cannot complete PIN/import/export on this card without a new secure-channel implementation.

## Work State
### Completed
- Repo restructure and all builds verified; device boots our firmware via `fastboot boot /tmp/opencode/boot-stub-test.img`; adb uid 0.
- Fixed: About QR, crypto (`crypto_fast` sha512/PBKDF2/HMAC), hmac shim, Vol+/Vol−, brightness, tamper check, mnemonic creation, NFC transport imports/auto-connect, MaixPy shims.
- NFC bring-up, discovery and activation now work end-to-end: card activates as `PVJCOP4SID`; AID SELECT and `B0 3C 00 00` (GET_STATUS) both return `90 00` with the same 13-byte status.
- Decoded card status: protocol 0.2, applet 0.1, PIN0 tries 5, PUK0 1, setup done, **`needs_secure_channel=1`**, NFC policy enabled.
- Identified `VERIFY_PIN` failure: `10 60 08 02 b2 00` = `CORE_INTERFACE_ERROR_NTF`, status `0xB2` = `RF_TIMEOUT_EXCEPTION`; no data reply, card then stops answering.
- Verified from `Toporin/Seedkeeper-Applet` v0.2-0.1 that plaintext instructions other than `0x3C`/`0x81`/`0x82` get `SW_SECURE_CHANNEL_REQUIRED (0x9C20)`; `VerifyPIN` is just `OwnerPIN.check`, so on-card PBKDF2 is not the cause.
- Patched `krux/src/nfc.py`: timestamped debug; `_data()` recognizes `CORE_INTERFACE_ERROR`/`CORE_GENERIC_ERROR`/`RF_DEACTIVATE`, names `0xB2`, marks link dead, 5 s timeout; `connect()` closes stale node before reopening.
- Added `0x9C20: "Secure channel required"` to `krux/src/krux/seedkeeper.py`.
- Deployed and restarted the firmware on the device.
- Appended the latest compaction event to `docs/compaction-log.md`.
- Added a detailed `Status 2026-10-28` section to `docs/nfc-isodep.md` covering the working trace, status decode, RF timeout, secure-channel requirement, and open questions.
- Changes to `krux/src/nfc.py`, `krux/src/krux/seedkeeper.py`, `docs/nfc-isodep.md`, `docs/compaction-log.md` are **uncommitted**.

### Active
- Awaiting user device test with the SeedKeeper held flat over the camera area for the whole PIN operation, to see whether the card now returns `9C20` or still reports the RF timeout.
- Need to determine whether the observed silence is link loss (card moved) or the card muting on gated commands.

### Blocked
- SeedKeeper on this stock card: plaintext-only protocol cannot work because `needs_secure_channel=1`; secure-channel support (INS `0x81`/`0x82`) is not implemented.
- Camera: MaixPy `sensor` API gap over V4L2; kernel-side camera backlog (`bytesused=0`/`pix_count=0`, CSID packet counts, `camif error status: 0xd0`, b25 buf_done untested).
- `/dev/mem` absent; SELinux blocks kernel-side state files under ROM Android.
- Splash: framebuffer renders but `MSMFB_DISPLAY_COMMIT` + `FBIOPAN_DISPLAY` do not flip the panel (deferred).

## Next Move
1. Inspect the fresh `/data/krux/fw.log` after the user's held-card test; confirm whether `VERIFY_PIN` yields `9C20` or remains silent.
2. If `9C20`: decide with the user whether to implement SeedKeeper secure channel (P-256 ECDH, HMAC-SHA160 session/MAC keys, AES-128-CBC, IV counters, INS `0x81`/`0x82`); would need AES/SHA1 exposed in `crypto_fast` and EC via embit.
3. If still silent: probe individual APDUs (`B0 A7 00 00`, malformed `B0 42 00 00 00`) with timestamps to distinguish card muting from RF link loss.
4. Commit the `nfc.py`/`seedkeeper.py`/docs changes with a clear message.
5. After NFC: start camera Phase A per `docs/cam-plan.md`; refresh `docs/continue-prompt.md`; defer splash.

## Relevant Files
- `krux/src/nfc.py`: transport, discovery/activation, `_data()` error handling/timeouts, reconnect logic; `DEBUG=1`.
- `krux/src/krux/seedkeeper.py`: APDU protocol/status words (`0x3C`, `0x42`, `0x9C20`).
- `docs/nfc-isodep.md`: new status section with full NFC trace, secure-channel findings, next tests.
- `docs/compaction-log.md`: appended compaction event.
- `docs/cam-state.md`: vendor bring-up/activation trace.
- `phase0/libnfc-nci-20791b05.conf`: vendor `NFA_DM_START_UP_CFG` (`B5:03:01:02:FF`).
- `micropython/ports/unix/nfc_bcm2079x.c`: `_nfc` C module (open/power/wake/read/write).
- `/tmp/opencode/fw_app2.log`: latest app packet log (activation + APDU + `0xB2` failure).
- `/tmp/opencode/SeedKeeper.java`: applet source used to prove secure-channel gating.
- `/tmp/opencode/CardConnector.py`: pysatochip reference for auto secure-channel handling.
- `scripts/build/build-micropython.sh`: firmware build (output `kruxport/micropython-arm32`).
- `scripts/deploy-firmware.sh`: pushes interpreter/src and restarts service.
- `docs/cam-plan.md`: camera completion plan.

## Important Context
- Project dir: `/home/user/Develop/src/krux-nexus`; submodules `krux` (`nexus-port`), `micropython` (`master`), `linux` (`airgap/master-cleanup`).
- Device `0879524b0068abca`; deploy with `DEPLOY_RESTART=1 bash scripts/deploy-firmware.sh`; firmware log `/data/krux/fw.log`; restart via `setprop ctl.stop/start krux`.
- Stopping the `krux` service blanks the screen; restart restores touch/panel after ~10 s.
- Only one process can usefully use `/dev/bcm2079x`; kill background listeners before app tests.
- NCI packet meanings: `10 60 06 03 01 00 01` = `CORE_CONN_CREDITS_NTF`; `10 60 08 02 b2 00` = `CORE_INTERFACE_ERROR_NTF`/`0xB2`; `10 61 07 01 00` = `RF_FIELD_INFO_NTF`; `10 60 00 02 00 01` = `CORE_RESET_NTF`.
- APDU framing: out `10 00 00 <len> <apdu>`, in `10 00 00 <len> <data><SW1><SW2>`.
- Activation notification tail: ATS-like `0E 78 77 71 02` + ASCII `PVJCOP4SID`; TB `0x71` gives FWI=7, SFGI=1.
- Secure channel protocol: INS `0x81` (client P-256 pubkey, card returns ephemeral key+sig), INS `0x82` (AES-128-CBC body + HMAC-SHA160, IV counter); reference `CardConnector.py`.
- MicroPython: no `traceback` (use `sys.print_exception`); builtin `hashlib` rejects `setattr`; `raise ... from` prints "Warning: exception chaining not supported".

## 2026-10-28 01:39:58 UTC - session compaction (auto)

* session: `ses_ec9cfaf7efferMLQZnvjWxHDO8`

## Objective
- Get Krux firmware (MicroPython port) fully working on the LG Nexus 5 (hammerhead) air-gapped device: fix device issues, then the camera, on our own kernel.

## Requirements
- Air-gapped: no modem/Wi-Fi/BT; no privileged/signature permissions; revertible by reflash; adb is the only debug channel.
- Minimize changes to upstream Krux so they can merge upstream; prefer port-level shims over firmware edits.
- Builds use `/tmp/opencode` for objects; kernel image must be `zImage-dtb`; `fastboot boot` for tests unless flashing is explicitly requested.
- Ask the user explicitly (prefix `\7`/BEL) whenever physical action on the device is required.
- “Recovery” means deploying MicroPython+Krux inside the TWRP ramdisk; Android-related work can be dropped.
- Preserve `docs/standalone-log.md` / `docs/compaction-log.md` except for appending compaction events.

## Decisions
- Keep MicroPython (not MaixPy); canonical port sources are in `krux/src/` and `micropython/ports/unix/`.
- `crypto_fast` (mbedtls) replaces slow pure-Python sha512/PBKDF2/HMAC; `hashlib.sha256` stays native.
- NFC discovery needs the vendor’s second round: `RF_DISCOVER` → `RF_DEACTIVATE` → `RF_SET_ROUTING` → `RF_DISCOVER`.
- But do **not** run that second round in bring-up when the initial poll may already have activated the card: it tears the session down before the first APDU.
- Current sequence: bring-up sends one `RF_DISCOVER`; `_discover()` restarts with deactivate/routing/discover only after ~1 s without a tag.
- `CORE_INTERFACE_ERROR` / `0xB2 RF_TIMEOUT_EXCEPTION` is terminal; the transport marks itself disconnected and reconnects on the next transmit.
- The stock SeedKeeper applet requires the secure channel (`needs_secure_channel=1`); plaintext `VERIFY_PIN` cannot succeed even with a perfect link.
- The card is not muting: it answers plaintext gated commands with `9C20` when they are sent promptly.
- `IsoDep.setTimeout` is host-side only and cannot override the NFCC’s RF timeout.

## Work State
### Completed
- Back-cover/antenna issue identified: the Nexus 5 NFC antenna is in the back cover; with the cover off the chip polled but never saw a card.
- With the cover in place, activation now succeeds as `PVJCOP4SID`; SELECT and GET_STATUS work on some attempts.
- Confirmed via raw probes that the card returns `9C20` for:
  - unknown gated instruction with 0, 1, 2, 3, 4, 5, 6, 10 bytes
  - `VERIFY_PIN` with 1, 2, 3, 4, 5, 6 bytes (including wrong 6-byte PIN `000000`)
- All `9C20` replies arrived in ~8–13 ms; PIN tries remained 5, so the wrong PIN was not processed.
- Reproduced the app failure: SELECT + GET_STATUS succeed, then the 6-byte `VERIFY_PIN` (`b0 42 00 00 06 "123456"`) is sent ~4.8 s after GET_STATUS and gets `0xB2` with no data.
- `nfc.py` now names `CORE_INTERFACE_ERROR`, `CORE_GENERIC_ERROR`, `RF_DEACTIVATE`, and statuses `0xB0–0xB3`; adds timestamps and a 5 s APDU timeout; closes stale nodes on reconnect.
- `seedkeeper.py` maps `0x9C20` to “Secure channel required”.
- Docs and earlier fixes committed: `krux` `c7bf220c`, repo `b1d4562`.
- Latest `nfc.py` discovery changes are deployed (device hash `64d4c4dc…`) but uncommitted.

### Active
- Testing whether the ISO-DEP session dies after a few seconds of idle time; the app’s PIN prompt leaves ~5 s between GET_STATUS and VERIFY.
- `/tmp/opencode/nfc_idle_probe.py` is written but not yet pushed or run. It checks GET_STATUS at 0 s, 2 s, 5 s, 10 s, then 6-byte VERIFY_PIN.
- If idle timeout is confirmed: add a reconnect/re-select or keepalive immediately before `verify_pin`.

### Blocked
- Actual SeedKeeper PIN/import/export still requires the secure channel (INS `0x81`/`0x82`); not implemented.
- First APDU after activation is sometimes lost (`0xB2` on SELECT), indicating a marginal/intermittent RF link even with the cover on.
- Camera: MaixPy `sensor` API gap over V4L2; kernel-side camera backlog (`bytesused=0`, `pix_count=0`, CSID/`camif error status: 0xd0`).
- Splash: framebuffer renders but `MSMFB_DISPLAY_COMMIT` + `FBIOPAN_DISPLAY` do not flip the panel.

## Next Move
1. Push `/tmp/opencode/nfc_idle_probe.py` to `/data/krux/`, stop the `krux` service, run it with the card held, then restart the service.
2. Read the first timeout after an idle gap; determine whether 2 s, 5 s, or 10 s kills the session.
3. If confirmed, make the app re-select/reconnect after PIN entry (or keep the link alive during the prompt), then retest.
4. Implement the SeedKeeper secure channel: fetch `Toporin/pysatochip` `SecureChannel.py`; needs P-256 ECDH, HMAC-SHA160, AES-128-CBC, IV counters, INS `0x81`/`0x82`.
5. Update `docs/nfc-isodep.md` with the second-round regression, confirmed `9C20` gate, and idle-timeout finding; commit `nfc.py` and docs.
6. After NFC, start camera Phase A per `docs/cam-plan.md`; defer splash.

## Relevant Files
- `krux/src/nfc.py`: transport, discovery/restart logic, activation, APDU timeout and link-error handling.
- `krux/src/krux/pages/seedkeeper.py`: `_connect()` flow — SELECT, GET_STATUS, PIN prompt, `verify_pin`.
- `krux/src/krux/seedkeeper.py`: APDU protocol, status words including `0x9C20`.
- `docs/nfc-isodep.md`: NFC architecture and the “Status 2026-10-28” findings; now partly superseded.
- `docs/cam-state.md`: vendor bring-up/activation trace and historical “second round required” note.
- `/tmp/opencode/nfc_idle_probe.py`: idle-gap probe, written and ready to push/run.
- `/tmp/opencode/nfc_len_probe.py`: length probe; all lengths returned `9C20`.
- `/tmp/opencode/nfc_apdu_probe.py`: retrying SELECT/gated-command probe.
- `/tmp/opencode/nfc_len_probe.out`: captured successful `9C20` results and status.
- `/tmp/opencode/fw_pin_rf.log`: app log showing SELECT/GET_STATUS success and VERIFY `0xB2`.
- `/tmp/opencode/SeedKeeper.java`: stock applet source; secure-channel gate and `VerifyPIN`.
- `/tmp/opencode/CardConnector.py`: pysatochip secure-channel reference (`0x81`/`0x82` formats).
- `scripts/deploy-firmware.sh`: pushes interpreter/src and restarts the service.
- `scripts/build/build-micropython.sh`: firmware build, output `kruxport/micropython-arm32`.
- `docs/cam-plan.md`: camera completion plan.

## Important Context
- Device `0879524b0068abca`, adb state `recovery`; deploy with `DEPLOY_RESTART=1 bash scripts/deploy-firmware.sh`; firmware log `/data/krux/fw.log`; restart via `setprop ctl.stop/start krux`.
- Only one process can use `/dev/bcm2079x`; stop `krux` before running probes and restart afterward.
- Stopping `krux` blanks the screen; restart restores touch/panel after ~20 s.
- APDU framing: out `10 00 00 <len> <apdu>`, in `10 00 00 <len> <data><SW1><SW2>`.
- Activation trace: `10 61 05 26 01 02 04 00 ff 01 …`, ATS tail `0E 78 77 71 02` + `PVJCOP4SID`; TB `0x71` → FWI=7, SFGI=1.
- Card status 13 bytes: protocol 0.2, applet 0.1, PIN0 tries 5, PUK0 1, setup done, `needs_secure_channel=1`, NFC policy enabled.
- `10 60 06 03 01 00 01` = `CORE_CONN_CREDITS_NTF`; `10 60 08 02 b2 00` = `CORE_INTERFACE_ERROR_NTF` / `0xB2`.
- The single-discovery variant produced endless `RF_DISCOVER` status `0x06` and no tag notifications; the restored second round produces `0x00` and polls normally.
- Adding the second round in bring-up caused activation followed by `0xB2` on the first APDU; moving the restart into `_discover()` fixed that.
- Raw probes sometimes fail at the first SELECT after activation; they retry SELECT until successful.
- App’s failing APDU is `b0 42 00 00 06 31 32 33 34 35 36` (“123456”), ~4.8 s after GET_STATUS; expected reply is `9C20`.
- Secure-channel request formats: `0x81` carries a 65-byte uncompressed P-256 public key; `0x82` carries `IV(16) | len(2) | ciphertext | len(2) | HMAC-SHA160(20)`.
- MicroPython: no `traceback` (use `sys.print_exception`); `raise ... from` prints “Warning: exception chaining not supported”.
- Build firmware with `bash scripts/build/build-micropython.sh` (~3 min, `BUILD=/tmp/opencode/mp-build`).

## 2026-10-28 01:57:41 UTC - session compaction (auto)

* session: `ses_ec9cfaf7efferMLQZnvjWxHDO8`

## Objective
- Get Krux firmware fully working on the LG Nexus 5 (hammerhead) as an air-gapped device: SeedKeeper NFC first, then camera, on our own kernel.

## Requirements
- Air-gapped: no modem, Wi-Fi, Bluetooth, privileged permissions, or signature permissions; adb is the only debug channel; reflash must be reversible.
- Minimize upstream Krux divergence; prefer port-level shims.
- Build objects under `/tmp/opencode`; kernel image is `zImage-dtb`; use `fastboot boot` unless flashing is explicitly requested.
- Ask explicitly with a BEL (`\7`) before any physical action on the device.
- Preserve `docs/standalone-log.md` and `docs/compaction-log.md` except for appending compaction events.
- For any PIN test use only `123456`; do not risk consuming PIN tries.

## Decisions
- Keep MicroPython, not MaixPy; port sources live in `krux/src/` and `micropython/ports/unix/`.
- `crypto_fast`/mbedtls provides slow hashes and PBKDF2; `hashlib.sha256` remains native.
- NFC discovery needs the vendor second round; current bring-up sends one `RF_DISCOVER` and `_discover()` restarts after ~1 s without a tag.
- `CORE_INTERFACE_ERROR` / `0xB2` is terminal; transport marks itself disconnected and reconnects on the next transmit.
- The stock SeedKeeper applet requires the secure channel; plaintext `VERIFY_PIN` returns `9C20`.
- The ISO-DEP link dies after roughly 3–5 seconds of idle; refresh/re-select before PIN verification and retry a lost first APDU.
- Secure channel uses **secp256k1**, not P-256, matching the applet’s `Secp256k1` key agreement.

## Work State
### Completed
- Confirmed the Nexus 5 NFC antenna is in the back cover; activation works as `PVJCOP4SID`.
- Confirmed SELECT and GET_STATUS success when the card is held well.
- Probed gated commands: unknown INS and `VERIFY_PIN` with 1–10 bytes, including 6-byte `123456`, all return `9C20` in ~8–13 ms when sent promptly.
- Idle probe: 3 s gap works; 5 s gap fails with `0xB2`; card returns `9C20` again after reconnect + SELECT.
- Confirmed the app’s ~4.8–6.2 s PIN-entry gap caused the original RF timeout.
- Added `SeedKeeper.refresh()` with one link-error retry; initial `_connect()` now uses it, and PIN verification/setup call it.
- App now reaches the expected `Secure channel required` error instead of RF timeout after PIN entry.
- Implemented `krux/src/krux/seedkeeper_sc.py`: secp256k1 ECDH, HMAC-SHA1 key derivation, AES-128-CBC, PKCS#7, IV counters, 0x81/0x82 framing, ephemeral-key recovery.
- Integrated secure-channel wrapping into `SeedKeeper._transmit`, added `needs_secure_channel` to `parse_card_status`, and clear the session on `select()`.
- Device self-test passed AES known vector, HMAC-SHA1, padding, and ECDH symmetry.
- Fixed `_random_bytes` because `urandom.getrandbits` accepts at most 32 bits.

### Active
- Secure-channel recovery and end-to-end verification against the real card are not yet validated.
- `seedkeeper_sc.py` still contains `[::-1]` step slices, which MicroPython does not support; must be replaced before deployment.
- Initial connection is still occasionally unstable: first attempt may lose GET_STATUS after a successful SELECT; second attempt works.
- SC integration is local only; the app currently on the device does **not** include it.

### Blocked
- Actual PIN verification/import/export waits on validating the secure-channel handshake.
- Camera work is deferred until NFC is complete.

## Next Move
1. Replace every extended slice (`[::-1]`) in `seedkeeper_sc.py` with a loop-based reverse helper.
2. Validate ECDSA public-key recovery with a known secp256k1 signature; verify recovered X/Y endianness and `ecdsa_recover` usage.
3. Deploy the SC integration with `DEPLOY_RESTART=1 bash scripts/deploy-firmware.sh`, then test `refresh()` → `card_status()` → `verify_pin("123456")` with the card held.
4. Inspect `/data/krux/fw.log` for 0x81/0x82 exchanges, outer `9000`, and decrypted responses; then test `memory_status()` and `list_secrets()`.
5. Harden initial connect by retrying SELECT + `card_status()` on a link error; then handle SC session loss/idle during authenticated operations.
6. Commit `nfc.py`, the SeedKeeper changes, and updated `docs/nfc-isodep.md`; then start camera Phase A.

## Relevant Files
- `krux/src/krux/seedkeeper_sc.py`: new secure-channel implementation; still needs MicroPython slice fix and recovery validation.
- `krux/src/krux/seedkeeper.py`: SC wrapping, `refresh()`, status parsing, `_random_bytes` fix.
- `krux/src/krux/pages/seedkeeper.py`: refreshes link before PIN verification/setup and retries initial selection.
- `krux/src/nfc.py`: ISO-DEP transport, idle/reconnect behavior, error naming; uncommitted changes from earlier work.
- `docs/nfc-isodep.md`: NFC findings; needs idle-timeout and secure-channel update.
- `/tmp/opencode/pysatochip/pysatochip/SecureChannel.py`: reference key derivation and encryption.
- `/tmp/opencode/pysatochip/pysatochip/CardConnector.py`: reference 0x81/0x82 handling and response decryption.
- `/tmp/opencode/pysatochip/pysatochip/CardDataParser.py`: reference 0x81 response parsing and signature recovery.
- `/tmp/opencode/SeedKeeper.java`: applet source for `InitiateSecureChannel`/`ProcessSecureChannel`.
- `/tmp/opencode/sc_selftest.py`: device crypto self-test; recovery test still fails at local signing.
- `/tmp/opencode/fw_sc_required.log`: log proving `9C20` after refresh.
- `/tmp/opencode/nfc_idle_probe.py`: idle-timeout probe and result source.
- `scripts/deploy-firmware.sh`: pushes interpreter/src and restarts the service.
- `docs/cam-plan.md`: camera work to resume after NFC.

## Important Context
- Device `0879524b0068abca`, adb state `recovery`; deploy with `DEPLOY_RESTART=1 bash scripts/deploy-firmware.sh`; firmware log `/data/krux/fw.log`.
- Only one process can use `/dev/bcm2079x`; stop `krux` before probes and restart afterward. Stopping blanks the screen; restart restores it after ~20 s.
- Use `/data/krux/micropython` for device-side scripts; logs show APDU framing as `10 00 00 <len> <apdu>`.
- Card status: protocol 0.2, applet 0.1, PIN0 tries 5, setup done, `needs_secure_channel=1`, NFC policy enabled.
- Secure channel: host sends `B0 81 00 00 41 04||X||Y`; card replies `00 20 || X(32) || siglen(2) || DER sig [|| auth sig]`; recover Y from the self-signature over `00 20 || X`.
- Derive `AES key = HMAC-SHA1(shared_x, "sc_key")[:16]`, `MAC key = HMAC-SHA1(shared_x, "sc_mac")`.
- 0x82 body: `IV(16) | len(2) | AES-CBC ciphertext | len(2) | HMAC-SHA1(20)`; IV is 12 random bytes + 4-byte counter, host starts at 1 and adds 2.
- Response: `IV(16) | len(2) | ciphertext`; no response MAC; decrypt and remove PKCS#7. Outer `9000` means success; inner SW is not returned.
- `ucryptolib.aes(key, ucryptolib.MODE_CBC, iv)` works on device. `urandom` has no `urandom(n)`; use 32-bit chunks. MicroPython lacks `[::-1]`.
- `parse_card_status` marks `needs_secure_channel=False` for the 11-byte FakeCard status, so existing plaintext tests remain unaffected.
- Local pytest is blocked by missing `Crypto` (`ModuleNotFoundError: Crypto.Cipher`); `py_compile` passes.
- Device signing self-test fails in `/data/krux/secp256k1.py` with `'int' object has no attribute 'bit_length'`; unrelated to SC recovery but prevents generating a local test signature.
- Current local changes are uncommitted; `nfc.py`, `seedkeeper.py`, page, and new SC module are not committed. `seedkeeper_sc.py` was manually pushed but still has the slice bug.

## 2026-10-28 02:26:20 UTC - session compaction (auto)

* session: `ses_ec9cfaf7efferMLQZnvjWxHDO8`

## Objective
- Get Krux firmware fully working on the LG Nexus 5 (hammerhead) as an air-gapped device: SeedKeeper NFC first, then camera, on our own kernel.

## Requirements
- Air-gapped: no modem, Wi-Fi, Bluetooth, privileged permissions, or signature permissions; adb is the only debug channel; reflash must be reversible.
- Minimize upstream Krux divergence; prefer port-level shims.
- Build objects under `/tmp/opencode`; kernel image is `zImage-dtb`; use `fastboot boot` unless flashing is explicitly requested.
- Ask explicitly with a BEL (`\7`) before any physical action on the device.
- Preserve `docs/standalone-log.md` and `docs/compaction-log.md` except for appending compaction events.
- For any PIN test use only `123456`; do not risk consuming PIN tries.
- User needs a clear on-screen signal/timing before tapping the card; card must be presented after the reader starts polling.

## Decisions
- Keep MicroPython, not MaixPy; port sources live in `krux/src/` and `micropython/ports/unix/`.
- `crypto_fast`/mbedtls provides slow hashes and PBKDF2; `hashlib.sha256` remains native.
- NFC discovery needs the vendor second round; `_discover()` restarts polling after ~1 s without a tag.
- `CORE_INTERFACE_ERROR` / `0xB2` is terminal; transport marks itself disconnected and reconnects on the next transmit.
- Secure channel uses **secp256k1**, HMAC-SHA1, AES-128-CBC; implemented with embit pure-Python curve arithmetic and device `ucryptolib`.
- The card must enter an already-running RF field: `IsoDepTransport.prepare()` now powers up and starts polling before the UI shows “Tap your SeedKeeper card”.
- `9C30` is the applet’s transient multi-step lock; it is retried automatically with a fresh IV/counter.
- App reconnects/retries the initial SELECT+GET_STATUS and the PIN verification once on link loss.
- Auto-retry/re-auth for read-only commands after a later link loss is still pending; it was planned but not implemented.

## Work State
### Completed
- Confirmed full secure-channel handshake against the real card: `0x81` succeeds, host ECDH/recovery works, and encrypted `0x82` commands are accepted.
- Successfully ran a device probe that did `refresh()` → `card_status()` → `_init_secure_channel()` → `verify_pin("123456")` → `memory_status()` → `list_secrets()`.
- Probe results: SC ready in 0.51 s; PIN verified in 0.06 s; memory `total=4095`, `free=3970`, `nb_secrets=1`; one secret `id=1`, label `twst`, type 16, subtype 1, fingerprint `5d6b5afe`.
- Implemented `krux/src/krux/seedkeeper_sc.py`: secp256k1 ECDH, manual ECDSA public-key recovery, HMAC-SHA1 key derivation, AES-128-CBC, PKCS#7, `0x81`/`0x82` framing, odd host IV counters.
- Added `needs_secure_channel` parsing from status byte 11, SC wrapping in `SeedKeeper._transmit`, SC reset on SELECT, and automatic SC setup on `9C20`/first secure command.
- Added `9C30` lock retry; app logs show `9C30` followed by a fresh envelope returning `9000`.
- Fixed MicroPython port issues: replaced `int.bit_length()` in `krux/src/secp256k1.py` and `krux/vendor/embit/src/embit/util/key.py`; fixed `urandom.getrandbits` >32-bit usage; removed `[::-1]` from SC code.
- Local signing now works after the `_bit_length` fix; AES known vector, HMAC-SHA1, ECDH symmetry, host secp256k1 signature recovery, and local sign/verify all pass.
- Added optional `IsoDepTransport.prepare()`; page now shows “Starting card reader” then starts the field before “Tap your SeedKeeper card”.
- App logs show at least one attempt successfully verified PIN and read memory status/list data; `0x9C12` correctly ends the list.
- `tests/test_seedkeeper.py`: 46 passed; 2 errors only because `pytest-mock`/`mocker` is unavailable in the local environment.

### Active
- App-level reliability: some attempts still end in `0xB2` RF timeout after PIN or after several secure commands; logs show PIN/list success in one attempt, then later `0x82` commands receive `B2`.
- Need decide between a keepalive while menus/list views are open and automatic reconnect + re-PIN for read-only commands after link loss.
- Need test the actual app flows (`View Secrets`, free space, card info, save/load) with the card held continuously.
- Secure-channel/session tests and updated documentation are not yet committed.

### Blocked
- Camera work remains deferred until SeedKeeper NFC is reliable.
- Import/export/save-to-card tests need explicit user permission because they modify card contents.
- Local full pytest still needs `pytest-mock`; two tests cannot run locally.

## Next Move
1. Reproduce the app `View Secrets` flow and inspect `/data/krux/fw.log` to determine whether `B2` follows idle time or card movement.
2. Implement either a card keepalive for open SeedKeeper menus or automatic reconnect + re-verify for read-only commands (`A6`, `A7`) using the stored PIN, with one retry limit.
3. Retest app-level SeedKeeper: initial tap prompt, PIN `123456`, view secrets, free space, card info; keep the card pressed throughout.
4. Add status mapping/tests for `9C21`, `9C22`, `9C23`, `9C30`, and SC envelope behavior; update `docs/nfc-isodep.md`.
5. Commit the uncommitted NFC/SeedKeeper/SC/port changes.
6. Start camera Phase A from `docs/cam-plan.md`.

## Relevant Files
- `krux/src/krux/seedkeeper_sc.py`: secure-channel implementation, manual secp256k1 recovery, AES/HMAC envelope.
- `krux/src/krux/seedkeeper.py`: SC integration, `refresh()`, status parsing, 9C30 retry, random-byte fixes.
- `krux/src/krux/pages/seedkeeper.py`: reader prepare timing, initial SELECT/status retry, PIN verify retry.
- `krux/src/nfc.py`: ISO-DEP transport, new `prepare()`/`prepared` flow, idle/reconnect behavior.
- `krux/src/secp256k1.py`: port copy with `_bit_length` fallback.
- `krux/vendor/embit/src/embit/util/key.py`: `_bit_length` fallback for signing.
- `scripts/deploy-firmware.sh`: now also pushes `embit/util/*.py`.
- `docs/nfc-isodep.md`: needs SC/idle/prepare findings.
- `docs/cam-plan.md`: deferred camera work.
- `/tmp/opencode/sc_probe.py`: successful device-side SC + PIN + list probe.
- `/tmp/opencode/fw_app_sc3.log`: latest app log with `9C30` retry and later `B2`.
- `/tmp/opencode/sc_recover_test.py`: host-vector recovery test.
- `/tmp/opencode/pysatochip/pysatochip/SecureChannel.py`: host SC reference.
- `/tmp/opencode/SeedKeeper.java`: applet reference for 0x81/0x82, lock, and status words.

## Important Context
- Device `0879524b0068abca`, adb state `recovery`; firmware log `/data/krux/fw.log`; deploy via `DEPLOY_RESTART=1 bash scripts/deploy-firmware.sh`.
- Only one process can use `/dev/bcm2079x`; stop `krux` before probes and restart after. Stopping blanks the screen; restart takes ~20 s.
- Service commands used: `adb shell 'setprop ctl.stop krux; sleep 3; ...; setprop ctl.start krux; sleep 18'`.
- Card status: protocol 0.2, applet 0.1, PIN0 tries 5, setup done, `needs_secure_channel=1`, NFC policy enabled.
- Secure channel details: host sends `B0 81 00 00 41 04||X||Y`; card returns `00 20 || X(32) || siglen(2) || DER sig [|| auth sig]`; recover Y from the self-signature over `00 20 || X`.
- Derive `AES key = HMAC-SHA1(shared_x, "sc_key")[:16]`, `MAC key = HMAC-SHA1(shared_x, "sc_mac")`.
- `0x82` body: `IV(16) | len(2) | AES-CBC ciphertext | len(2) | HMAC-SHA1(20)`; IV is 12 random bytes + 4-byte counter, host starts at 1 and adds 2.
- Response body: `IV(16) | len(2) | ciphertext`; no response MAC; decrypt and remove PKCS#7. Outer `9000` means success; inner SW is not returned.
- Applet uses `ALG_EC_SVDP_DH_PLAIN_XY`, which outputs `0x04 || X || Y`; its HMAC input starts at offset 1, so the host’s full shared X is correct.
- `9C30` lock is transient: `resetLockThenThrow()` clears it; retrying the same command with a new IV succeeds (observed).
- `9C12` means “no more secrets” and is the normal end of `list_secrets`.
- `B2` means the RF link timed out; it can indicate card movement/coupling or an idle link. The transport reconnects on the next transmit, but re-selecting the applet logs out.
- Device crypto: `ucryptolib.aes(key, MODE_CBC, iv)` works; `urandom.getrandbits` maxes at 32 bits; MicroPython lacks `int.bit_length` and extended slices.
- Local tests need `PYTHONPATH=/tmp/opencode/pydeps` for the temporary `pycryptodome` install; `pytest-mock` is still missing.
- Current app/device files were pushed and the service restarted, but all source changes remain uncommitted.

## 2026-10-28 03:00:26 UTC - session compaction (auto)

* session: `ses_ec9cfaf7efferMLQZnvjWxHDO8`

## Objective
- Get Krux firmware fully working on the LG Nexus 5 (hammerhead) as an air-gapped device: SeedKeeper NFC first, then camera, on our own kernel.

## Requirements
- Air-gapped: no modem, Wi-Fi, Bluetooth, privileged permissions, or signature permissions; adb is the only debug channel; reflash must be reversible.
- Minimize upstream Krux divergence; prefer port-level shims.
- Build objects under `/tmp/opencode`; kernel image is `zImage-dtb`; use `fastboot boot` unless flashing is explicitly requested.
- Ask explicitly with a BEL (`\7`) before any physical action on the device.
- Preserve `docs/standalone-log.md` and `docs/compaction-log.md` except for appending compaction events.
- For any PIN test use only `123456`; do not risk consuming PIN tries.
- The user needs a clear on-screen signal/timing before tapping the card; the card must be presented after the reader starts polling, and held in place.
- Do not modify card contents (import/export/delete/save) without explicit permission.

## Decisions
- Keep MicroPython, not MaixPy; port sources live in `krux/src/` and `micropython/ports/unix/`.
- `crypto_fast`/mbedtls provides slow hashes and PBKDF2; `hashlib.sha256` remains native.
- Secure channel uses secp256k1, HMAC-SHA1, AES-128-CBC; implemented with embit pure-Python curve arithmetic and device `ucryptolib`.
- Recover the card's ephemeral Y with manual ECDSA recovery; do not use the platform `ecdsa_recover`, which depends on missing `int.bit_length()`.
- Use `IsoDepTransport.prepare()` to start polling before showing the tap prompt; show a second tap/hold prompt after PIN entry.
- Use plaintext `GET_STATUS` keepalives between host SC recovery and ECDH to avoid the NFCC idle-link timeout.
- `9C30` is the applet's transient multi-step lock; retry it automatically with a fresh IV/counter.
- Keep the PIN only in the `SeedKeeper` object's memory for the session and auto-reconnect/re-verify for read-only recovery; restart stateful walks rather than repeating one APDU.
- Camera work remains deferred until SeedKeeper NFC is reliable.

## Work State
### Completed
- Confirmed full secure-channel handshake and PIN verification against the real card.
- Probe results: SC ready in 0.51 s; PIN verified in 0.06 s; memory `total=4095`, `free=3970`, `nb_secrets=1`; one secret `id=1`, label `twst`, type 16, subtype 1, fingerprint `5d6b5afe`.
- Implemented `krux/src/krux/seedkeeper_sc.py`: secp256k1 ECDH, manual public-key recovery, HMAC-SHA1 key derivation, AES-128-CBC, PKCS#7, `0x81`/`0x82`, odd host counters, and GET_STATUS keepalives.
- Added SC wrapping, status parsing, `9C30` retry, `9C20` auto-setup, and secure-session reset on SELECT.
- Added `IsoDepTransport.prepare()`, post-PIN second prompt, "Starting card reader"/"Hold the SeedKeeper card" UI, and `ACTIVATE_SETTLE_S = 0.05`.
- Added auto-relogin with stored PIN for `INS_SEEDKEEPER_STATUS`; `list_secrets()` restarts after link loss.
- Fixed MicroPython port issues: `int.bit_length()` in `secp256k1.py` and `embit/util/key.py`; extended-slice usage in SC; `urandom.getrandbits` >32-bit usage.
- `tests/test_seedkeeper.py`: 46 passed; 2 errors only because `pytest-mock`/`mocker` is unavailable locally.
- App-level success: after the second hold prompt, loading a mnemonic, listing secrets, and Card Info have worked.
- `view_secrets` display duration increased to `FLASH_MSG_TIME * 3`; not yet retested.

### Active
- The first card read of a session is still slow and often fails; retries eventually succeed.
- The second command after PIN is flaky: View Secrets/list works, View Free Space usually fails, Card Info works only occasionally.
- User suspects the failures are message-length or timing dependent: "works for view secrets. doesn't work for read free space. and works occasionlly for card info."
- `refresh()` was just changed to retry SELECT 3 times but has not been compiled, deployed, or tested.
- Need stronger app-level retry/feedback for post-PIN actions, especially `free_space()` and `card_info()`.
- Need update `docs/nfc-isodep.md` with SC, keepalive, second-tap, and timing findings.
- All source changes remain uncommitted; `krux` submodule is dirty.

### Blocked
- Import/export/save/delete tests need explicit permission because they modify card contents.
- Camera work remains deferred until SeedKeeper NFC is reliable.
- Local full pytest still needs `pytest-mock`; `pycryptodome` is installed under `/tmp/opencode/pydeps`.

## Next Move
1. Compile/deploy the current `seedkeeper.py` with 3-attempt `refresh()`; increase page retry loops and add a visible "Retrying card" state.
2. Add a page-level read retry helper using `_reader_prompt()` + `_login_again()` for `free_space()`, `card_info()`, and `view_secrets()`.
3. Test View Free Space repeatedly, then Card Info/View Secrets, holding the card through the second prompt; pull `/data/krux/fw.log`.
4. If B2 persists, compare successful/failed APDU timing and consider an A7-specific or transport-level timing fix.
5. Run the SeedKeeper tests, update `docs/nfc-isodep.md`, and commit the NFC/SC/port changes.
6. Start camera Phase A from `docs/cam-plan.md`.

## Relevant Files
- `krux/src/krux/seedkeeper.py`: SC integration, retries, status parsing, `_login_again`, stored session PIN.
- `krux/src/krux/seedkeeper_sc.py`: secure-channel handshake, recovery, keepalives, AES/HMAC envelope.
- `krux/src/krux/pages/seedkeeper.py`: prepare/tap/hold prompts, second tap after PIN, page flows and retries.
- `krux/src/nfc.py`: ISO-DEP transport, `prepare()`, activation delay, idle/reconnect behavior.
- `krux/src/secp256k1.py`: port `_bit_length`/`_reverse` shims.
- `krux/vendor/embit/src/embit/util/key.py`: `_bit_length` shim for signing.
- `scripts/deploy-firmware.sh`: now pushes `embit/util/*.py`.
- `docs/nfc-isodep.md`: transport findings; needs SC/timing update.
- `docs/cam-plan.md`: deferred camera work.
- `tests/test_seedkeeper.py`: SeedKeeper/transport tests.
- `/tmp/opencode/fw_app_sc7.log`: latest app log with SC keepalives and B2 failures.
- `/tmp/opencode/sc_probe.py`: successful device-side SC + PIN + list probe.
- `/tmp/opencode/sc_timing.py`: device timings: recover 149 ms, keygen 34 ms, ECDH 34 ms.
- `/tmp/opencode/SeedKeeper.java`: applet reference for SC, lock, and status words.
- `/tmp/opencode/pysatochip/pysatochip/SecureChannel.py`: host SC reference.

## Important Context
- Device `0879524b0068abca`, adb state `recovery`; firmware log `/data/krux/fw.log`; deploy via `DEPLOY_RESTART=1 bash scripts/deploy-firmware.sh`.
- Only one process can use `/dev/bcm2079x`; stop `krux` before probes and restart after. Stopping blanks the screen; restart takes ~20 s.
- Service commands: `adb shell 'setprop ctl.stop krux; sleep 3; ...; setprop ctl.start krux; sleep 18'`.
- Card status: protocol 0.2, applet 0.1, PIN0 tries 5, setup done, `needs_secure_channel=1`, NFC policy enabled.
- B2 is the NFCC's `RF_TIMEOUT_EXCEPTION`, not the host timeout. It can occur on the first SELECT, on `0x81`, or on the first `0x82`.
- Successful `0x81` responses arrive in ~300 ms; successful `0x81`→`0x82` gaps were ~226–233 ms, failing gaps ~337 ms.
- SC handshake details: send `B0 81 00 00 41 04||X||Y`; card returns `00 20 || X || siglen || DER`; recover Y from the self-signature over `00 20 || X`.
- Keys: `AES = HMAC-SHA1(shared_x, "sc_key")[:16]`, `MAC = HMAC-SHA1(shared_x, "sc_mac")`; IV is 12 random bytes + odd 32-bit counter starting at 1.
- `0x82` request body: `IV(16) | len(2) | AES-CBC ciphertext | len(2) | HMAC-SHA1(20)`; response is `IV(16) | len(2) | ciphertext` with no response MAC.
- `9C12` ends `list_secrets`; `9C30` is transient and cleared by `resetLockThenThrow`.
- The app now: initial prepare/tap → status → PIN prompt → second prepare/"Hold" prompt → verify → menu.
- `_select_secret()` auto-selects when there is exactly one seed, so "Load mnemonic" not showing a picker is expected.
- `View Secrets` currently only flashes headers; it does not open an interactive selection screen.
- Local pytest needs `PYTHONPATH=/tmp/opencode/pydeps`; remaining two tests need `pytest-mock`.
- Current git state: `krux` submodule modified; `scripts/deploy-firmware.sh` and `docs/compaction-log.md` modified; `.opencode/` untracked.

## 2026-10-28 04:07:38 UTC - session compaction (auto)

* session: `ses_ec9cfaf7efferMLQZnvjWxHDO8`

## Objective
- Get Krux firmware fully working on the LG Nexus 5 (hammerhead) as an air-gapped device: SeedKeeper NFC first, then camera, on our own kernel.

## Requirements
- Air-gapped: no modem, Wi-Fi, Bluetooth, privileged permissions, or signature permissions; adb is the only debug channel; reflash must be reversible.
- Minimize upstream Krux divergence; prefer port-level shims.
- Build objects under `/tmp/opencode`; kernel image is `zImage-dtb`; use `fastboot boot` unless flashing is explicitly requested.
- Ask explicitly with a BEL (`\7`) before any physical action on the device.
- Preserve `docs/standalone-log.md` and `docs/compaction-log.md` except for appending compaction events.
- For any PIN test use only `123456`; do not risk consuming PIN tries.
- User needs a clear on-screen signal/timing before presenting the card; the card must be presented after the reader starts polling and held in place.
- Do not modify card contents (import/export/delete/save) without explicit permission.
- Camera work remains deferred until SeedKeeper NFC is reliable.

## Decisions
- Keep MicroPython, not MaixPy; port sources live in `krux/src/` and `micropython/ports/unix/`.
- `crypto_fast`/mbedtls provides slow hashes and PBKDF2; `hashlib.sha256` remains native.
- Secure channel uses secp256k1, HMAC-SHA1, AES-128-CBC; implemented with embit pure-Python curve arithmetic and device `ucryptolib`.
- Recover the card's ephemeral Y with manual ECDSA recovery; do not use the platform `ecdsa_recover`.
- Use keepalives after the 0x81 response, between host key recovery and ECDH; do **not** send GET_STATUS immediately before 0x81.
- The app must prompt again after PIN entry and after label/prompt entry, because the link drops while the user uses the keypad.
- Reuse the verified PIN from memory for read retries and pre-write reconnects.
- Import must send `header[2:]`; sending `header[4:]` caused the applet's `0x9C0F` invalid-parameter response.
- Use `.digest().hex()` for fingerprints; this MicroPython hashlib lacks `hexdigest()`.
- Camera work remains deferred.

## Work State
### Completed
- Confirmed full secure-channel handshake and PIN verification against the real card.
- Probe results: SC ready in 0.51 s; PIN verified in 0.06 s; memory `total=4095`, `free=3970`, `nb_secrets=1`; original secret `id=1`, label `twst`, type 16, subtype 1, fingerprint `5d6b5afe`.
- Implemented SC, manual key recovery, HMAC-SHA1, AES-128-CBC, GET_STATUS keepalives, `9C30` retry, and secure-session reset on SELECT.
- Added `IsoDepTransport.prepare()`, initial card prompt, post-PIN second prompt, post-label reauth, and `_card_action` retry for read-only actions.
- Confirmed loads, list, Card Info, and free space can work; save successfully wrote a new secret, verified by View Secrets.
- Fixed import header offset and `sha256.hexdigest` crash, updated the fake card in `tests/test_seedkeeper.py`.
- Increased retries: `refresh()` tries SELECT 3 times; `_connect` retries status and PIN verification 3 times.
- Retry text now clears the display; View Secrets duration increased to 3× `FLASH_MSG_TIME`.
- Tests: 46 passed; 2 errors only because `pytest-mock`/`mocker` is unavailable locally.

### Active
- NFC reliability remains poor: first read and especially 0x81 handshake often fail with `CORE_INTERFACE_ERROR`/B2; many retries needed.
- Second save attempt failed with RF error after many retries; general reads remain unreliable.
- Investigating the vendor NFC stack for ISO-DEP RF-timeout configuration or recovery behavior; relevant leads are `NFC_PMID_FWI`/param `0x58`, `NCI_PARAM_ID_NFCDEP_TRANS_TIME 0xB2`, and `nfa_dm_discover.c` interface-error handling.
- Need to decide between retrying on the same activation, altering activation settle timing, or changing NFCC config; `ACTIVATE_SETTLE_S = 0.2` did not clearly help.
- Need to update `docs/nfc-isodep.md` and commit the NFC/SC/port changes.

### Blocked
- Further import/save/delete tests require explicit permission because the card now holds at least the original `twst` secret plus a newly saved secret.
- Local full pytest still needs `pytest-mock`; `pycryptodome` is installed under `/tmp/opencode/pydeps`.
- Camera work remains deferred until SeedKeeper NFC is reliable.

## Next Move
1. Continue vendor-stack analysis in `nfa_dm_discover.c`, `nfc_hal_dm_cfg.c`, `nfc_brcm_defs.h`, and `probes/nci_defs.h` for the ISO-DEP RF timeout, FWI/WTX, or interface-error retry path.
2. Test whether a 0x81 B2 can be retried on the same activation instead of power-cycling; compare success rates and timings.
3. Re-test reads and one save only after explicit permission; use View Secrets first to avoid duplicate secrets.
4. Pull `/data/krux/fw.log`, count successful 0x81 responses vs B2, and compare activation/settle timing.
5. Run the SeedKeeper tests, update `docs/nfc-isodep.md`, and commit the NFC/SC/port changes.
6. Start camera Phase A from `docs/cam-plan.md` only after SeedKeeper is reliable.

## Relevant Files
- `krux/src/krux/seedkeeper.py`: SC integration, import header fix, fingerprint fix, retries, stored PIN.
- `krux/src/krux/seedkeeper_sc.py`: handshake, recovery, keepalives, AES/HMAC envelope.
- `krux/src/krux/pages/seedkeeper.py`: prompts, reauth, page-level retries, save/delete flows.
- `krux/src/nfc.py`: ISO-DEP transport, activation settle, discovery, B2 handling.
- `krux/tests/test_seedkeeper.py`: 46 tests plus updated fake-card import parsing.
- `docs/nfc-isodep.md`: transport/SC findings; needs latest timing and import notes.
- `nfc/stack/nfa/dm/nfa_dm_discover.c`: vendor discovery, FWI, interface-error handling.
- `nfc/halimpl/hal/nfc_hal_dm_cfg.c`: vendor NFC HAL configuration.
- `nfc/vendor-headers/nfc_brcm_defs.h`: Broadcom NCI parameters, including `NCI_PARAM_ID_NFCDEP_TRANS_TIME`.
- `probes/nci_defs.h`: NCI parameter IDs and lengths.
- `phase0/libnfc-nci-20791b05.conf`: vendor `NFA_DM_START_UP_CFG`.
- `/tmp/opencode/fw_app_sc12.log`: latest app log with handshake successes and B2 failures.
- `/tmp/opencode/SeedKeeper.java`: applet reference for import/SC/status words.
- `/tmp/opencode/sc_timing.py`: device timings: recover 149 ms, keygen 34 ms, ECDH 34 ms.
- `scripts/deploy-firmware.sh`: deploys firmware and embit files; use `DEPLOY_RESTART=1`.

## Important Context
- Device `0879524b0068abca`, adb state `recovery`; firmware log `/data/krux/fw.log`; deploy via `DEPLOY_RESTART=1 bash scripts/deploy-firmware.sh`.
- Only one process can use `/dev/bcm2079x`; stop `krux` before probes and restart after. Stopping blanks the screen; restart takes ~20 s.
- Service commands: `adb shell 'setprop ctl.stop krux; sleep 3; setprop ctl.start krux; sleep 18'`; a second restart may be needed if the screen stays blank.
- Card works reliably on another device, so the remaining failures are in our BCM2079x/NFC transport path or timing.
- B2 is the NFCC's `RF_TIMEOUT_EXCEPTION`, not the host timeout; it occurs on first SELECT, 0x81, or first 0x82.
- Successful 0x81 responses arrive ~300 ms after send; failed ones report B2 around 175–220 ms, with occasional longer delays.
- A GET_STATUS immediately before 0x81 made every handshake fail; it was removed. Keepalives after the 0x81 response remain.
- 0x9C0F is the applet's `Invalid parameter`; the old import payload skipped two extra header bytes.
- Save flow: `_connect` → label keypad → `_reconnect` → memory status → `import_secret`; a link failure during memory status now aborts before writing.
- Applet import header layout starts at type: type, origin, export_rights, counters/fingerprint, subtype, RFU2, label_size, label, padded size.
- `_connect` no longer swallows link errors from `memory_status` during save; a protocol status still allows a card-side capacity judgment.
- Current state: app is running and screen visible; reads mostly work but require many retries; one save succeeded, a later one failed with RF.
- All source changes remain uncommitted; the `krux` submodule is dirty; `tests/test_seedkeeper.py` is also modified.

## 2026-10-28 04:57:28 UTC - session compaction (auto)

* session: `ses_ec9cfaf7efferMLQZnvjWxHDO8`

## Objective
- Get Krux firmware fully working on the LG Nexus 5 (hammerhead) as an air-gapped device: SeedKeeper NFC first, then camera, on our own kernel.

## Requirements
- Air-gapped: no modem, Wi-Fi, Bluetooth, privileged permissions, or signature permissions; adb is the only debug channel; reflash must be reversible.
- Minimize upstream Krux divergence; prefer port-level shims.
- Build objects under `/tmp/opencode`; kernel image is `zImage-dtb`; use `fastboot boot` unless flashing is explicitly requested.
- Ask explicitly with a BEL (`\7`) before any physical action on the device.
- Preserve `docs/standalone-log.md` and `docs/compaction-log.md` except for appending compaction events.
- For any PIN test use only `123456`; do not risk consuming PIN tries.
- User needs a clear on-screen signal/timing before presenting the card; the card must be presented after the reader starts polling and held in place.
- Do not modify card contents (import/export/delete/save) without explicit permission.
- Camera work remains deferred until SeedKeeper NFC is reliable.
- User now explicitly wants longer read windows, plenty of retries, and an ability to cancel an operation.

## Decisions
- Keep MicroPython, not MaixPy; port sources live in `krux/src/` and `micropython/ports/unix/`.
- `crypto_fast`/mbedtls provides slow hashes and PBKDF2; `hashlib.sha256` remains native.
- Secure channel uses secp256k1, HMAC-SHA1, AES-128-CBC; implemented with embit pure-Python curve arithmetic and device `ucryptolib`.
- Recover the card's ephemeral Y with manual ECDSA recovery; do not use the platform `ecdsa_recover`.
- Use keepalives after the 0x81 response, between host key recovery and ECDH; do **not** send GET_STATUS immediately before 0x81.
- The app must prompt for PIN/label before presenting the card, then keep one uninterrupted card session; this avoids the link dying during keypad entry.
- Import must send `header[2:]`; sending `header[4:]` caused the applet's `0x9C0F` invalid-parameter response.
- Use `.digest().hex()` for fingerprints; this MicroPython hashlib lacks `hexdigest()`.
- Do not keep chasing RF timing: Android on the same phone is also inconsistent, so the remaining failures are hardware/coupling, not the ISO-DEP driver.
- Reverted the experimental FWI=15 and `PA_FSDI=2` changes back to the vendor values.

## Work State
### Completed
- Confirmed full secure-channel handshake and PIN verification against the real card.
- Confirmed loads, list, Card Info, free space, and save can work; a new secret was successfully written and appears in View Secrets.
- Fixed import header offset (`header[2:]`), `sha256.hexdigest` crash, and the fake-card tests.
- Removed the GET_STATUS immediately before 0x81; that was the regression that made every handshake fail.
- Confirmed the card also reads inconsistently in Android on the same Nexus 5, including multi-second reads and duplicate reads.
- Tested NFCC FWI=15: accepted by the chip but no reliability improvement; reverted.
- Tested `PA_FSDI=2` (32-byte reader frames): accepted but no improvement/worse; reverted.
- Tests: 46 passed; 2 errors only because `pytest-mock`/`mocker` is unavailable locally.
- Started a page-level retry/cancel refactor in `pages/seedkeeper.py`: added `CARD_ATTEMPT_TIMEOUT_MS=8000`, `CARD_SESSION_ATTEMPTS=20`, `CARD_ACTION_RETRIES=3`, `CARD_RETRY_PAUSE_S=0.1`, `_cancel_requested()`, `_retry_screen()`, `_card_session()`, PIN-before-card `_connect()`, retrying `_reconnect()` and `_card_action()`.

### Active
- Finish and validate the SeedKeeper page UX refactor.
- `save_seed()` still needs its label prompt moved before `_connect()` and the extra `_reconnect()` removed, so Save uses one card presentation.
- Compile/test the page changes, deploy, and have the user test View Secrets/Load with cancel/retry behavior.
- Need to verify `_card_session()`/`_cancel_requested()` interaction, especially taps arriving during a blocking 8 s activation.
- Update `docs/nfc-isodep.md` with the Android comparison, failed FWI/PA_FSDI experiments, and the retry/cancel design.
- Commit the NFC/SC/page changes; all source changes remain uncommitted and the `krux` submodule is dirty.

### Blocked
- Further import/save/delete tests require explicit permission because the card holds at least the original `twst` secret plus newly saved secret(s).
- Camera work remains deferred until SeedKeeper NFC is reliable.

## Next Move
1. Finish `save_seed()`: prompt label before `_connect()`, then call `_write_seed()` immediately with no second card presentation.
2. Review the page refactor for correctness, run `py_compile` and the SeedKeeper tests.
3. Deploy `nfc.py` (vendor FWI restored) and `pages/seedkeeper.py`, restart the service, and ask the user with BEL to reboot into our firmware and test View Secrets/Load.
4. If reliability is still poor, use the user's rooted Android environment to collect NFC logs/comparisons and consider antenna/contact/placement fixes.
5. Update `docs/nfc-isodep.md` and commit the changes.
6. Start camera Phase A from `docs/cam-plan.md` only after SeedKeeper is reliable.

## Relevant Files
- `krux/src/krux/pages/seedkeeper.py`: page retry/cancel refactor; PIN-before-card flow; `save_seed()` still needs reordering.
- `krux/src/krux/seedkeeper.py`: SC integration, import header fix, fingerprint fix, stored PIN, retries.
- `krux/src/krux/seedkeeper_sc.py`: handshake, manual key recovery, keepalives, AES/HMAC envelope.
- `krux/src/nfc.py`: ISO-DEP transport; experimental FWI/PA_FSDI changes reverted; activation settle is 0.2 s.
- `krux/tests/test_seedkeeper.py`: 46 tests plus updated fake-card import parsing.
- `docs/nfc-isodep.md`: transport/SC findings; needs Android comparison and failed RF experiments.
- `docs/compaction-log.md`: append-only history; preserve.
- `scripts/deploy-firmware.sh`: deploy/restart; use `DEPLOY_RESTART=1`.
- `/tmp/opencode/fw_fsdi2.log`: latest PA_FSDI run showing repeated SELECT/0x81 B2 failures.
- `/tmp/opencode/fw_fwi15b.log`: FWI=15 run showing no improvement.
- `/tmp/opencode/fw_app_sc12.log`: earlier log with one successful 0x81 after many B2 failures.
- `/tmp/opencode/SeedKeeper.java`: applet reference for import/SC/status words.
- `phase0/libnfc-nci-20791b05.conf`: vendor `NFA_DM_START_UP_CFG`, `PREINIT_DSP_CFG`.
- `nfc/vendor-headers/nfc_brcm_defs.h`: Broadcom NCI parameter IDs, including `PA_FSDI` and `FRAME_INTF_RETXN`.

## Important Context
- Device `0879524b0068abca`; currently booted into Android with root (`su` via adb), not our recovery firmware.
- Firmware log is `/data/krux/fw.log`; deploy with `DEPLOY_RESTART=1 bash scripts/deploy-firmware.sh`.
- Only one process can use `/dev/bcm2079x`; stop `krux` before probes and restart after. Stopping blanks the screen; restart takes ~20 s.
- Service commands: `adb shell 'setprop ctl.stop krux; sleep 3; setprop ctl.start krux; sleep 18'`; a second restart may be needed if the screen stays blank.
- The Nexus 5 NFC antenna is in the back cover around the camera; the user tried multiple flat positions, orientations, shifts, and pressure, with random results.
- Android's own NFC reader is also inconsistent on this phone, including 10 s reads and duplicate reads, so the remaining problem is coupling/hardware rather than our ISO-DEP code.
- `0xB2` is `CORE_INTERFACE_ERROR`/`RF_TIMEOUT_EXCEPTION`; after it, the NFCC tears the RF link down, so retrying on the same activation does not work.
- Successful 0x81 responses take ~300 ms; failed ones report B2 around 175–220 ms, occasionally longer.
- `0x9C0F` is the applet's `Invalid parameter`; the old import payload skipped two extra header bytes.
- Current card contents include the original `twst` secret plus at least one newly saved secret; do not write/delete without explicit permission.
- All source changes remain uncommitted; the `krux` submodule is dirty; `tests/test_seedkeeper.py` is modified.

---

## Compaction 2026-10-28 — SeedKeeper complete on the second phone

The work in the preceding summary is finished.  The second Nexus 5 has reliable
NFC; the card can stay attached for the whole sequence and saving works.

- PIN (and the save label) are requested **before** the card is presented;
  one uninterrupted select → secure channel → verify session follows.
- The SeedKeeper page retries 20 sessions of 8 s, retries reads with a fresh
  secure channel, and offers tap-to-cancel between attempts.  Card Info is a
  PIN-free link test.
- `FWI=15` and `PA_FSDI=2` were tried and reverted; the vendor RF config is
  restored.
- Host tests: 82 passed for the SeedKeeper protocol and page suites.
- `kruxport/` is removed.  The interpreter is `rootfs/micropython-arm32`;
  `build-stub-ramdisk.sh --bundle` produces a read-only `/opt/krux` image and
  the init stub prefers it over `/data/krux`.
- `standalone-log.md` is now `project-log.md`; the README carries the old
  `kruxport/README.md` build/deploy text.
- NFC `.ncd` blobs are reference only; the runtime does not load them.

Next: rebuild kernel + MicroPython + boot image, final NFC check on the second
phone, then commit the submodules and the main project.  Camera Phase A follows.

## 2026-10-29 18:09:50 UTC - session compaction (manual)

* session: `ses_e7aba84c4ffewu0RUZojGclCWS`

## Objective
Bring up the IMX179 camera on the air-gapped LG Nexus 5 for Krux (QR scanning and entropy collection), progressing through 4 documented stages from kernel research to MicroPython integration.

## Requirements
- Work only on the camera; do not touch SeedKeeper or NFC.
- No modem, Wi-Fi, Bluetooth, vendor HAL, or Android framework in the final path.
- Reversible boots only (`fastboot boot`, never flash).
- One device, one thread at a time (camera boot+run cycle is atomic).
- Keep all port code under `krux/src/` and `micropython/ports/unix/`.

## Decisions
- **CAMIF_RAW + CPP v1.2.0** chosen as the stream source (PIX_VIEWFINDER blocked by VFE0 pix0 overflow; only raw path delivers frames).
- **Path A (native V4L2 + camd daemon)** chosen over Path B (HAL3); HAL3 depends on vendor HAL/gralloc/binder.
- **Hard-float toolchain** (`arm-linux-gnueabihf-*`) adopted for all recovery binaries and kernel builds; soft-float dependency dropped.
- **Unix socket** (`/data/krux/camd.sock`) instead of TCP for air-gap compliance.
- **devtmpfs toggled per build**: `=y` for camera builds (runtime nodes), `=n` for air-gap default. Currently `=y`.
- **KernelSu cherry-picked** (5 commits from `origin/lineage-23.2-kernelsu`) onto `airgap/cam-ksu` branch for root access during testing; linux submodule pointer stays on `airgap/master-cleanup` (95f4b0f) in main repo.
- **Image is dark** (Y mean≈1-2) due to fixed exposure; auto-exposure is a future optimization. QR testing requires bright lighting.

## Work State
### Completed
- Stage 1: devtmpfs enabled, camera kernel built and booted, VFE overflow documented as blocker.
- Stage 2: CAMIF_RAW path delivers non-empty NV12 frames (~900k non-zero bytes, frame0.nv12 saved).
- Stage 3: camd daemon (captures QVGA, serves over Unix socket), sensor.py (MaixPy-compatible client), _image native module (Image, width, height, to_bytes, from_nv12, get_statistics, find_qrcodes via quirc), image.py wrapper, lcd_bcm.c (Image support). Smoke test passes (10 frames 320×240, get_id() → 0x179). 100-frame stability passed (mem_free stable). Unix socket verified (no TCP listener).
- Toolchain: switched all scripts and docs from soft-float to hard-float.
- Docs: cam-state.md, cam-plan.md, cam-bringup.md, cam-bringup-stage1-4.md, continue-prompt.md, project-log.md, directories.md all updated.
- All changes committed across main repo, krux, and micropython submodules.

### Active
- Stage 4 acceptance: QR decode on real QR code, scene-dependent entropy, 50 reset/run/close cycles, 30-min stability, air-gap verification.

### Blocked
- QR decode untested (image too dark, needs bright lighting or auto-exposure).
- VFE0 pix0 overflow blocks PIX_VIEWFINDER path (only CAMIF_RAW works).
- Device instability under sustained load (thermal/watchdog reboots).
- TWRP overlays LCD writes (visual confirmation limited in recovery mode).
- Air-gap verification not yet run.

## Next Move
1. Boot camera kernel (`fastboot boot artifacts/built/boot-cam.img`), verify recovery + uid 0 + device nodes.
2. Deploy firmware (`DEPLOY_RESTART=1 bash scripts/deploy-firmware.sh`).
3. Run test scripts in order: `test_smoke.sh`, `test_qr.sh` (hold bright QR code 15-25 cm), `test_entropy.sh`, `test_lcd.sh`, `test_stability.sh`, `test_cycles.sh`.
4. Boot air-gap kernel, run `test_airgap.sh`.
5. If QR fails due to dark image, implement auto-exposure via `__write_reg` in camd.
6. 30-min long-run stability test on QR capture page.

## Relevant Files
- `cam/camd.c`: daemon (capture + QVGA conversion + Unix socket server)
- `cam/imaging8.c`: lab tool (measurement fixes, CPP retry)
- `cam/imx179_regs.h`: sensor register tables (settings_7 for binned mode)
- `krux/src/sensor.py`: MaixPy-compatible camd client
- `krux/src/image.py`: Python wrapper around native _image
- `micropython/ports/unix/image_maixpy.c`: native _image module (quirc integrated)
- `micropython/ports/unix/lcd_bcm.c`: LCD display (Image support added)
- `micropython/ports/unix/Makefile`: registered image_maixpy.c + quirc sources
- `scripts/deploy-firmware.sh`: pushes micropython, krux, camd, cpp firmware
- `tools/build-kernel.sh`: kernel build (host toolchain, camera profile)
- `docs/cam-bringup-stage4.md`: Stage 4 prompt with tests and acceptance
- `docs/cam-state.md`: current camera state
- `artifacts/built/boot-cam.img`: camera kernel boot image

## Important Context
- Camera char-device majors are allocated at runtime; devtmpfs must be on for camera builds.
- `CONFIG_DEVTMPFS` is currently `=y` in `tools/profiles/hammerhead-keep.list` (restored from `=n` for testing). Air-gap builds should use `=n`.
- The kernelsu branch (`airgap/cam-ksu`) is available in linux/ for root access but the main repo pointer stays on `airgap/master-cleanup`.
- All commits are pushed to local branches; nothing pushed to origin.
- Test scripts are in `scripts/misc/test_*.sh`. The device is unstable under sustained load (thermal/watchdog); use shorter runs.
- The image is dark (Y mean≈1-2) due to fixed exposure (1248/1252 lines). QR detection requires bright lighting.
- `camd` must be started externally (no subprocess in MicroPython); sensor.py assumes it's already running.

## 2026-10-30 18:53:42 UTC - session compaction (manual)

* session: `ses_e7aba84c4ffewu0RUZojGclCWS`

## Objective
- Bring up the IMX179 camera on the air-gapped LG Nexus 5 for Krux (QR scanning and entropy), now focused on implementing automatic brightness (auto-exposure) end to end.
- Determine whether device crashes during camera testing are caused by our binaries/kernel or by overheating from repeated Android boots.

## Requirements
- Work only on the camera; do not touch SeedKeeper or NFC.
- Final path must exclude modem, Wi-Fi, Bluetooth, vendor HAL, and Android framework.
- Reversible boots only (`fastboot boot`, never flash).
- One device, one thread at a time; camera boot+run cycle is atomic.
- Keep port code under `krux/src/` and `micropython/ports/unix/`.
- Use a kernel with KernelSU patches for root access.
- Device camera will be exposed to a decently lit scene for testing.
- Complete work without user interaction where possible; if device becomes unusable or a decision is needed, message the user with an ASCII 0x07 (BEL) character.
- After Android boot, allow a cooldown delay: try 3 minutes, then 5 minutes if 3 is insufficient.
- User wants thermal evidence sampled: (1) after boot completes, (2) after cooldown delay, (3) during the test.

## Decisions
- `CAMIF_RAW` + CPP v1.2.0 remains the capture path (`PIX_VIEWFINDER` blocked by `VFE0 pix0 overflow`).
- Path A (native V4L2 + camd daemon) over HAL3; Unix socket `/data/krux/camd.sock` for air-gap compliance.
- Hard-float toolchain (`arm-linux-gnueabihf-*`) for all recovery binaries and kernel builds.
- devtmpfs `=y` for camera builds, `=n` for air-gap default; currently `=y`.
- KernelSU cherry-picks live on `airgap/cam-ksu`; main repo `linux` pointer stays on `airgap/master-cleanup` (95f4b0f).
- AE exposed via camd socket commands: `W` (write reg: reg_hi,reg_lo,val), `E` (exposure hi/lo + gain hi/lo), `V` (VTS hi/lo). `ae-patch.py` is the source of truth; source stays clean and patches apply at build time.
- Group hold (reg `0x0104`: 1=hold, 0=release) added around AE writes; it appears in the vendor init tables (`imx179_settings_3`/`_6`).

## Work State
### Completed
- Stages 1–3: camera kernel boots with devtmpfs, `CAMIF_RAW` delivers NV12 frames, camd + sensor.py + native `_image` (quirc) + LCD display working; smoke and 100-frame stability passed.
- Docs reorganized: `docs/plan-camera.md` → `docs/cam-plan.md`; `continue-prompt.md` dropped; `docs/todo.md` created.
- Built and committed VFE overflow analysis + tooling (`docs/cam-bringup-vfe.md`, `tools/vfe-instrument.py`, `scripts/cam/build-vfe-kernel.sh`, `test-vfe.sh`, `compare-vfe.sh`).
- Auto-exposure Steps 1–2 done and committed: camd `W`/`E`/`V` commands with group hold; `sensor.py` gained `__write_reg`, `set_exposure`, `set_gain`, `set_exposure_gain`, `set_vts`.
- Verified on device: camd produces frames (`GOT A FRAME`, ~1.9M non-zero bytes); CPP firmware loads (`chromatrix sent`, status 0); `W` returns `b'OK'`; `set_exposure`/`set_gain` succeed; register writes reach sensor (`[sensor] wrote 1 registers`).
- **Register writes DO change brightness**: baseline Y mean 5 → 9 after exposure=2000 (+80%), measured after 5-min cooldown.
- All changes committed and clean across main repo, `krux`, `micropython`; nothing pushed to origin.

### Active
- (none)

### Blocked
- Device crashes/reboots within ~10–15 s of camd running; each crash boots Android (several minutes at peak load), which heats the device.
- Cause unresolved: user suspects our software may crash the kernel rather than pure overheating; no thermal/panic evidence captured yet (`/proc/last_kmsg` and pstore absent; dmesg only shows KernelSU `sys_reboot` entries).
- AE Steps 3–7 (brightness/VTS sweep, AE algorithm, move AE into camd C, Krux page integration) not done.
- VFE overflow fix not implemented (source analysis + tooling only).
- Stage 4 acceptance (QR on real code, scene-dependent entropy, 50 cycles, 30-min stability, air-gap audit) not run.

## Next Move
1. Boot KernelSU camera kernel (`/tmp/opencode/boot-ksucam.img`), wait 5 min.
2. Sample thermal zones (`/sys/class/thermal/thermal_zone*/temp`) after boot, after cooldown, and during the test; compare with crash timing and dmesg to determine thermal vs software fault.
3. Run the brightness test again including `set_vts()` to raise frame length, then `sweep-exposure.sh`.
4. If brightness scales, implement AE algorithm in Python, then port to camd (C) and integrate Krux pages.
5. Return to VFE fix and Stage 4 acceptance once device is stable.

## Relevant Files
- `docs/cam-bringup-auto-exposure.md`: current AE plan, commands, and recorded test results.
- `tools/ae-patch.py`: canonical AE patch source (W/E/V commands + group hold).
- `cam/camd.c`: daemon; socket command loop and sensor write helpers.
- `krux/src/sensor.py`: client API including `__write_reg`/`set_exposure`/`set_gain`/`set_exposure_gain`/`set_vts`.
- `cam/imx179_regs.h`: register tables; `settings_7` = binned mode; `0x0104` group hold.
- `scripts/cam/build-ae-camd.sh`: builds static AE camd.
- `scripts/cam/test-registers.sh`, `test-ae.sh`, `sweep-exposure.sh`: AE test scripts.
- `docs/cam-bringup-vfe.md`, `tools/vfe-instrument.py`, `scripts/cam/build-vfe-kernel.sh`: VFE investigation.
- `docs/todo.md`: remaining project tasks.
- `docs/cam-state.md`, `docs/cam-plan.md`, `docs/project-log.md`: current state and decisions.
- `rootfs/bootimg.py`: unpack/pack boot images.
- `/home/user/Develop/src/krux-nexus/artifacts/twrp-3.7.0_9-HH.R.17-MADV_WIPEONFORK.img`: ramdisk source for boots.

## Important Context
- KernelSU camera boot image is `/tmp/opencode/boot-ksucam.img` (~21 MB), built from `linux` branch `airgap/cam-ksu` (`ee54a78345f`) plus TWRP ramdisk; rebuild if `/tmp` is wiped.
- Standard camd invocation: `./camd 1640 1232 0x40000000 rear 0 /data/krux/cpp_firmware_v1_2_0.fw 2 binned cfg nocycle 0 0x2b 0 0 300 0 0 0 0x12 0 1 0xffffffff 0 0 0 1 0`.
- Kill holders before camd: `mm-qcamera-daemon` and `cameraserver`; binaries at `/vendor/bin/mm-qcamera-daemon` and `/system/vendor/bin/hw/android.hardware.camera.provider-service.lineage` (rename to `.bak`).
- Root access: in recovery shell often uid=0 directly; `su` may be missing. In Android, `su -c` works (KernelSU). Some commands silently fail without root — check `id`.
- IMX179 binned mode exposure is 1248 of VTS 1252 lines; raising exposure beyond that requires increasing VTS (`0x0340/0x0341`).
- Gain alone did not raise brightness after exposure maxed; exposure change did (5 → 9).
- Tool `sleep 180`/`300` exceeds the 120 s shell timeout; wait in 60 s chunks.
- camd frame rate in recovery is ~0.01–0.2 fps; first frame takes ~8–10 s.
- `camd` must be started externally (no subprocess in MicroPython); `sensor.py` assumes it is running.
- Repair/rename of camera daemons is undone by reboot; repeat each boot.

## 2026-10-31 01:21:52 UTC - session compaction (auto)

* session: `ses_e7aba84c4ffewu0RUZojGclCWS`

## Objective
- Bring up the IMX179 rear camera on the air-gapped LG Nexus 5 for Krux QR scanning (animated UR) and entropy, currently focused on reaching 20 fps capture / 15 fps decode.
- Immediate task: capture the vendor’s **binned 1640×1232 preview** VFE/ISP configuration to fix the PIX_VIEWFINDER overflow; keep CAMIF_RAW+CPP at ~15 fps as fallback.

## Requirements
- Camera only; do not touch SeedKeeper or NFC.
- Final path must exclude modem, Wi-Fi, Bluetooth, vendor HAL, and Android framework.
- Reversible boots only (`fastboot boot`, never flash); camera boot+run cycle atomic; one device/thread at a time.
- Keep port code under `krux/src/` and `micropython/ports/unix/`; use a KernelSU kernel for root during testing.
- Animated UR 5–10 fps; primary QR source is LCD/OLED screen; target **20 fps capture / 15 fps decode**, floor 15/10 for 5 fps UR; 8 fps only static-QR floor.
- Exposure lock after convergence is acceptable; entropy uses separate mid-gray/no-clip policy.
- Prefer option #1 (capture binned preview config); option #2 (accept ~15 fps CAMIF_RAW) is fallback.
- If device becomes unusable or a decision is needed, message the user with ASCII BEL (0x07).

## Decisions
- CAMIF_RAW+CPP is the stable current path (~14–15 fps); 20 fps requires PIX_VIEWFINDER/VFE.
- Kernel CPP panic fix is kept: `MSM_CPP_DUMP_FRM_CMD=0`, timeout handler drops frame and returns (no CPP writes/resend), stats IRQ guarded.
- 320 MHz VFE clock, full-line CAMIF `last_pixel=1639`, `cam_cycle`/`cam_capture`, and PIX0-enable writes do **not** fix PIX overflow.
- Full-height CBCR + full-height chroma buffer removes ping-pong (0) but stalls CPP after ~24 frames; half-height remains default.
- Existing vendor capture is **full-res 3280×2464 only**; binned preview config must be captured.
- CAPTURE: correct vendor tree is p25 system partition `/system/vendor`; p27 is cache, not vendor.

## Work State
### Completed
- Frame-path profile and optimization: debug path gated; 3×3 wide-load downscale; camd-only 22–28 fps; Python decode 2–5 ms, render 0–9 ms.
- Kernel CPP panic fixed on `airgap/cam-ksu` commit `6a10b79fc03`; stable boot image `/tmp/opencode/boot-fix-nocons.img`; no dump/timeout/stats panic/watchdog/IOMMU after fix.
- VFE instrumentation build/run: hw `0x1001001a` (8974 V2), 320 MHz; CAMIF_RAW 93 frames/0 overflow; PIX_VIEWFINDER 0 frames/301–599 overflow, 1 `camif violation`.
- Rejected VFE fixes: clock 320 MHz, `last_pixel=1639`, `cam_cycle`/`cam_capture`, VFE `CTRL_0=0x80` before/after `CONFIG_DONE`.
- Decoded existing capture: sensor full-res 3280×2464, VTS 2504, HTS 3440; no binned mode.
- Built debug-capture kernel `/tmp/opencode/ko-cap` and `boot-cap.img`; Android boots with it.
- Restored vendor daemons on p25 `/system/vendor`: `mm-qcamera-daemon` and `android.hardware.camera.provider-service.lineage`.
- Confirmed `cameraserver` (pid 625) and `mm-qcamera-daemon` (pid 707) are running in capture Android.

### Active
- Capture binned vendor preview configuration by running the vendor camera on the debug-capture Android build.
- First attempt produced only `sensor_cfg_0_n62.bin`, `sensor_cfg_1_n92.bin`, `sensorio_0/1/2`, and `isp_cfg_0/1_n1_l4.bin`; no preview VFE cycle.

### Blocked
- Camera app reports `CameraManagerGlobal: Camera service is unavailable` even though `cameraserver` and `mm-qcamera-daemon` are running; app returns to home screen, so preview does not complete.
- Capture files are root-only (`-rw-rw---- root root`); `adb pull` as shell fails — must pull via TWRP.
- `sensorio_2_t6_s3026325600.bin` write returned `-22` (huge bogus size; capture artifact, possibly not fatal).
- `boot-cap2.img` (capture kernel + installed DTB) hung before adb/fastboot; avoid.
- `boot-cap3.img` incorrectly mounts p27 at `/system/vendor`; wrong because p27 is cache; avoid.

## Next Move
1. Boot TWRP (`/tmp/opencode/boot-fix-nocons.img`), mount `/data`, pull `/data/local/tmp/*.bin`; parse `sensor_cfg_0_n62`/`_1_n92` with `tools/cam-capture-summary.py` to confirm binned 1640×1232 geometry.
2. Fix the vendor camera preview: inspect `dumpsys media.camera`, app logcat, camera permission/provider state; try `STILL_IMAGE_CAMERA`, grant `android.permission.CAMERA`, restart `vendor.camera.provider`/`cameraserver`, or use video record to force the binned mode.
3. Capture a full initial + per-frame ISP cycle for the binned session; pull via TWRP.
4. Generate binned `cam_config.h`/`cam_cycle.h` with `cam/tools/gen-cam-config.py`; compile a shadow-header `camd-binned`; test PIX_VIEWFINDER (source 1) on `/tmp/opencode/boot-fix-nocons.img`.
5. If PIX works, verify 20 fps and proceed to AE; otherwise fall back to CAMIF_RAW ~15 fps and document the 20 fps gap.

## Relevant Files
- `docs/cam-bringup-vfe.md`: VFE results, rejected fixes, next binned-preview capture.
- `docs/cam-frame-path.md`: frame-path timings, 20 fps gap, throughput ceiling.
- `docs/cam-stability-thermal.md`: kernel crash/IOMMU/CPP evidence and thermal verdict.
- `cam/camd.c`: CAMIF geometry 1639, `CAMD_FULL_CHROMA`, profile controls; PIX0 helper removed.
- `cam/data/vendor/`: full-res vendor capture (`sensor/`, `seq/`, `cyc/`).
- `cam/tools/gen-cam-config.py`: generates `cam_config.h`/`cam_cycle.h`.
- `tools/cam-capture-summary.py`: sensor geometry + ISP cycle summary.
- `/tmp/opencode/ko-cap/`: debug-capture kernel build.
- `/tmp/opencode/boot-cap.img`: capture kernel + stock Android ramdisk (vendor fixed via p25).
- `/tmp/opencode/boot-fix-nocons.img`: stable CPP-fix kernel/TWRP recovery.
- `/tmp/opencode/linux-ksu`: `airgap/cam-ksu` worktree, CPP fix commit `6a10b79fc03`.
- `/tmp/opencode/linux-cap`: `airgap/debug-capture` worktree with capture code.
- `krux/src/sensor.py`: has `set_vts` but not deployed to `/data/krux/sensor.py`.
- `scripts/cam/compare-vfe.sh`, `scripts/cam/test-vfe.sh`: VFE parameter extraction/run helpers.

## Important Context
- Vendor tree: `/vendor -> /system/vendor` on p25; p27 is `/cache`; daemons restored under `/system/vendor/bin`.
- Capture kernel config/DTB does **not** mount vendor; do not mount p27 as vendor. `boot-cap.img` with stock ramdisk uses p25 `/system/vendor` correctly.
- Capture kernel is `airgap/debug-capture` `e9bf776e758`; writes VFE/sensor/CPP captures to `/data/local/tmp` with kernel credentials, files root-only.
- Capture files seen: `sensor_cfg_0_n62.bin`, `sensor_cfg_1_n92.bin`, `isp_cfg_0_n1_l4.bin`, `isp_cfg_1_n1_l4.bin`, `sensorio_0/1/2`.
- Existing full-res sensor geometry: W 3280, H 2464, VTS 2504, HTS 3440; binned would be W 1640, H 1232, VTS ~1252.
- Camera app `org.lineageos.aperture`; video activity `org.lineageos.aperture/.VideoCamera`; logcat shows repeated `Camera service is unavailable`.
- Stable kernel has CCP fix; VFE experimental 320 MHz image `/tmp/opencode/boot-vfe-fix-nocons.img` and instrumentation were reverted from the worktree.
- Main repo `linux` pointer remains `airgap/master-cleanup`; kernel fix commit lives on `airgap/cam-ksu`.
- Commits: `2da79b6` (full-line CAMIF geometry/compare fix), `7dd550f` (VFE results), `ee54b5a` (vfe-instrument cleanup), `389b8f1` (full-chroma/profile), `85377a8` (frame-path profile), `6a10b79fc03` (kernel CPP fix).
- Device currently in Android with capture kernel; no KSU/root; `adb root` disabled (`ro.debuggable=0`); use TWRP for root/file pulls.

## 2026-10-31 13:38:05 UTC - session compaction (auto)

* session: `ses_e7aba84c4ffewu0RUZojGclCWS`

## Objective
- Finish the Nexus 5 camera/Krux integration, then implement SeedKeeper UI and on-card new-mnemonic generation while real-device testing is unavailable.
- Latest user task: reformat “View Secrets”, replace fixed-delay SeedKeeper info screens with dismiss-on-touch dummy menus, and add New Mnemonic → SeedKeeper Card.

## Requirements
- Camera work: reversible `fastboot boot` only; one device/thread; air-gapped path excludes modem, Wi-Fi, Bluetooth, vendor HAL and Android framework; target 20 fps capture / 15 fps decode, floor 15/10 for 5 fps UR; PIX optional for later.
- Camera tests must be one atomic boot+run cycle; do not reuse a dirty camera boot.
- SeedKeeper task: no real device testing until the user is available; implement/test offline where possible.
- “View Secrets” must be a selection menu like Load/Delete; SeedKeeper full-screen fixed-delay info must become a dummy menu requiring touch.
- New Mnemonic must include a SeedKeeper Card option alongside Camera, Words, D6 and D20.
- Use `https://github.com/Toporin/Seedkeeper-Applet` / `Specifications.md` as the SeedKeeper API reference.

## Decisions
- Camera fallback remains CAMIF_RAW + CPP; PIX_VIEWFINDER is optional.
- Y-only VFE completion is used for QR/entropy: only Y is in the composite group; half-height CBCR writes but is not waited on.
- CAMIF `0x660` error IRQ is masked for the Y-only path; SOF/EOF and violation paths remain.
- Stable camera image is `/tmp/opencode/boot-fix-nocons.img` (y-only + CAMIF mask); matching camd is `/tmp/opencode/camd-yonly`.
- Python client throttles to 15 fps while camd runs unpaced.
- SeedKeeper new mnemonic plan: use applet `INS_GENERATE_RANDOM_SECRET=0xA3` with `INS_GENERATE_MASTERSEED=0xA0` fallback, export 16/32 random bytes as BIP-39 entropy, import a BIP-39-format Masterseed, delete the temporary secret, then load the words.
- SeedKeeper dummy info screen: one-item `Menu` (`OK`, `back_label=None`, `offset=height-2*FONT_HEIGHT`) with info text drawn above.

## Work State
### Completed
- Camera wiring: camd snapshot protocol, `image.from_nv12` lifetime/args, luma stats/clipping, 15 fps client throttle, `shannon.entropy_img16b`, init-stub camd startup source, camd test harness.
- Camera kernel: `airgap/master-cleanup` has CPP fix `cdcc09b3682`, y-only completion `916fb726a2a`, CAMIF mask `bc0ddf04a08`.
- 10-minute stream passed: 50,798 camd frames, max 63 °C, 0 ping-pong, 0 tasklet, 410 CPP `EAGAIN`, 146 CAMIF lines, no watchdog.
- 1,000-snapshot test passed after correcting the test: `gc.mem_free` 863,264, loss 2,304 bytes, 0 ping-pong/tasklet, no watchdog.
- Test script fixed to `gc.collect()` before/after and every 100 frames, and `del img`.
- Camera docs updated; main tree and `linux` submodule clean.
- Main commits: `5580508` (y-only), `45ee6fc` (CAMIF), `122b757` (10 min/1,000 acceptance).
- Submodule commits reauthored: `micropython e01fbb28f`, `krux ff9a5cd9`; main integration/docs commits also Eugene.
- SeedKeeper API researched; no SeedKeeper code changes made yet.

### Active
- Implement SeedKeeper changes:
  - `View Secrets` selection menu and secret detail dummy menu.
  - Replace fixed-delay info in `free_space()` and `card_info()` with dummy menu.
  - Add SeedKeeper new-mnemonic generation to `Login.new_key()`.

### Blocked
- Real-device SeedKeeper testing: user unavailable.
- Camera real QR/UR and entropy pages, AE policy: device testing deferred.
- Current `boot-fix-nocons.img` is the TWRP test image; the final firmware init-stub/ramdisk with automatic camd startup still needs rebuilding.
- Stable fast camd lacks `W/E/V`; `Camera._auto_expose()` remains disabled.

## Next Move
1. Implement `_info_menu()` in `krux/src/krux/pages/seedkeeper.py`; use it for secret details, free space and card info.
2. Rework `view_secrets()` to list headers in a `Menu`, then show the selected secret in `_info_menu()`.
3. Add `INS_GENERATE_RANDOM_SECRET`/`INS_GENERATE_MASTERSEED` methods to `krux/src/krux/seedkeeper.py`; add `0x9C26`/unsupported fallback handling.
4. Add `SeedKeeperPage.create_new_mnemonic()`: PIN/card, card RNG entropy, BIP-39 derivation, label prompt, BIP-39 import, temp-secret delete, `load_key_fn(words)`.
5. Add “Via SeedKeeper Card” to `Login.new_key()` when `seedkeeper_available()`.
6. Run host/simulator tests; commit SeedKeeper work separately.
7. When device is available: real QR/UR, entropy, AE, SeedKeeper card tests, final init-stub boot image.

## Relevant Files
- `krux/src/krux/pages/seedkeeper.py`: View Secrets, Free Space, Card Info, card session and new-mnemonic page logic.
- `krux/src/krux/seedkeeper.py`: APDU protocol; needs A3/A0 generate commands.
- `krux/src/krux/pages/login.py`: `Login.new_key()` menu and new-mnemonic callbacks.
- `krux/src/krux/pages/__init__.py`: `Page`, `Menu`, `flash_text`, `_clicked_item`.
- `krux/src/krux/display.py`: `FONT_HEIGHT`, `DEFAULT_PADDING`, drawing helpers.
- `krux/src/krux/pages/mnemonic_loader.py`: `choose_len_mnemonic`, `_load_key_from_words`.
- `krux/src/krux/pages/home_pages/mnemonic_backup.py`: SeedKeeper save path.
- `cam/camd.c`: y-only `scmd.y_only`; camd socket/W/E/V.
- `cam/isp.h` and `linux/include/media/msmb_isp.h`: `y_only` stream ABI.
- `linux/drivers/media/platform/msm/camera_v2/isp/msm_isp_util.c`: y-only reapply and CAMIF error mask.
- `linux/drivers/media/platform/msm/camera_v2/isp/msm_isp40.c`: per-WM IRQ clearing.
- `linux/drivers/media/platform/msm/camera_v2/isp/msm_isp_axi_util.c`: composite mask and done-buffer plane selection.
- `docs/cam-integration.md`, `docs/cam-state.md`, `docs/cam-stability-thermal.md`: camera status and test results.
- `/tmp/opencode/boot-fix-nocons.img`, `/tmp/opencode/camd-yonly`: stable camera test artifacts.
- `scripts/misc/test_stability.sh`: corrected GC-aware 1,000-snapshot test.

## Important Context
- Device is currently in TWRP on the stable y-only kernel; `camd` is stopped.
- Stable camera image kernel reports `3.4.113-...-g916fb726a2a-dirty`; source now has the CAMIF commit `bc0ddf04a08`; rebuild from clean source if exact provenance is needed.
- 10-minute test thermal max was `tsens_tz_sensor6=63`; no thermal trip.
- First 1,000-snapshot run falsely reported `LEAK` (536,384 bytes) because it did not call `gc.collect()`; corrected test is stable at 2,304 bytes loss.
- `camd` deployed at `/data/krux/camd`; fast source is `/tmp/opencode/camd-85377.c`; y-only binary is `/tmp/opencode/camd-yonly`.
- SeedKeeper applet v0.2 `generateRandomSecret` APDU: `CLA=0xB0, INS=0xA3, P1=secret_size, P2=export_rights`, data `[secret_type, subtype, flag_save_entropy, label_size, label, entropy_size, entropy]`; returns `id(2)+fingerprint(4)`. It stores `TYPE_MASTERSEED=0x10` as `[size|secret]`, secret = `sha512(external_entropy|internal_entropy)` trimmed.
- Deprecated `INS_GENERATE_MASTERSEED=0xA0`: P1 seed size 16-64, P2 export rights, data `[label_size|label]`; same return; useful as v0.1 fallback.
- Current BIP-39 import format: `sk.pack_bip39_secret(master_seed, entropy, passphrase)` + `sk.make_header(TYPE_MASTERSEED, label, subtype=SUBTYPE_BIP39)`.
- `View Secrets` currently loops `flash_text(... duration=FLASH_MSG_TIME*3)`; `free_space`/`card_info` do the same.
- `Menu` adds a Back item unless `back_label=None`; `run_loop()` returns `(index,status)`; `back_index` is the last item.
- Main tree and `linux`/`krux`/`micropython` submodules are clean; no uncommitted changes.
- Kernel fix exists on `airgap/master-cleanup`; do not merge the KernelSU commits from `airgap/cam-ksu` into the shipping kernel.

## 2026-10-31 22:39:09 UTC - session compaction (manual)

* session: `ses_e7aba84c4ffewu0RUZojGclCWS`

## Objective
- Maintain the Nexus Krux fork with full upstream functionality parity and a standalone host test suite.
- Restore UR support and move MicroPython/cUR dependencies into the Krux fork so Krux can be reused independently.

## Requirements
- No symlinks in the Krux tree.
- cUR must be a single submodule, owned by the MicroPython fork, with its `micropython.mk` incorporated into the MicroPython port.
- MicroPython must be a submodule of Krux so Krux does not depend on the Nexus main project.
- Host tests must not require a full MicroPython firmware build.
- Document `UV_PROJECT_ENVIRONMENT` usage for the Krux test suite.
- `Kboot` must be deinitialized if not needed.
- Restore full upstream UR functionality.
- Preserve essential `/tmp` camera artifacts before host reboot.
- Camera test constraints: reversible `fastboot boot`, one atomic boot+run cycle, no modem/Wi-Fi/BT/vendor HAL; target 20 fps capture / 15 fps decode.
- SeedKeeper and UR real-device testing remain deferred until the user is available.

## Decisions
- Top-level project submodules are now only `linux` and `krux`.
- MicroPython is `krux/firmware/micropython`; cUR is `krux/firmware/micropython/lib/cUR`.
- Host `uUR` is built by `uv` as a CPython extension from `firmware/micropython/lib/cUR`.
- Firmware cUR is built by `krux/firmware/micropython/ports/unix/modules/cUR/micropython.mk`; build staging copies source into a disposable build dir, avoiding symlinks.
- `Kboot` was deinitialized; `MaixPy` remains uninitialized; `firmware/font` is normal tracked source used only by simulator/tooling.
- Essential camera artifacts were preserved under `artifacts/built/cam-yonly/`.
- The stable camera image is y-only + CAMIF-mask; exact stable daemon source is preserved because it differs from current `cam/camd.c`.

## Work State

### Completed
- SeedKeeper UI work: View Secrets menu, touch-dismissed info menus, SeedKeeper-generated new mnemonic.
- Host test suite green: `1433 passed, 1 xfailed`.
- Port tests repaired for buttons, touch, light, auto-shutdown, QR capture, printers, datum, menu.
- `hmac.compare_digest` shim and host `time.ticks_add`/`ticks_diff` shims added.
- UR support restored: `uUR` now compiled into firmware; PSBT/UR mnemonic/descriptor/Datum paths available again.
- cUR moved into the MicroPython fork; no cUR wrapper/symlinks remain in Krux.
- MicroPython moved into Krux; top-level main-project `micropython` submodule removed.
- Standalone Krux `uv sync` and full test suite verified from `krux/`.
- Firmware cross-build with nested cUR succeeded; `rootfs/micropython-arm32` updated.
- Docs updated for cUR, standalone test setup, external `UV_PROJECT_ENVIRONMENT`, and camera artifact paths.
- Kboot deinitialized and stale `.git/modules` caches cleaned.
- Essential `/tmp` artifacts preserved before reboot.

### Active
- Verify and push nested MicroPython/cUR commits to their remotes so fresh clones resolve them.
- Decide whether to restore thermal/GRBL printer parity, which still depends on missing `fpioa_manager`/UART pinmux.
- Real-device validation of UR mnemonic/PSBT/descriptor/Datum flows.
- Real-device validation of SeedKeeper menus and card-generated mnemonic.
- Final firmware init-stub/ramdisk with automatic camd startup.

### Blocked
- Real-device testing is unavailable until the user is available.
- Thermal/GRBL printer parity needs a Linux serial/USB backend; not solved by cUR/MicroPython work.
- Camera QR/UR/entropy pages and AE policy still require device testing; `Camera._auto_expose()` remains disabled because stable camd lacks `W`/`E`/`V`.

## Next Move
1. Verify/push the nested `micropython`, `lib/cUR`, and `krux` commits; validate a fresh recursive clone.
2. Run the documented standalone Krux setup from a clean checkout and confirm `uv sync` + tests.
3. Decide the printer parity scope, then implement a Linux serial backend or explicitly document it as unsupported.
4. With the device available, test UR mnemonic loading, PSBT signing/export, wallet descriptor UR, and Datum UR.
5. With the device available, test SeedKeeper View Secrets/info menus and card-generated mnemonic.
6. Rebuild final init-stub/ramdisk using `artifacts/built/cam-yonly/zImage-dtb`, add automatic camd startup, and test.
7. Continue camera QR/UR/entropy and AE testing on a fresh camera boot.

## Relevant Files
- `krux/firmware/micropython`: nested MicroPython submodule in Krux.
- `krux/firmware/micropython/lib/cUR`: single cUR submodule, commit `ed744562`.
- `krux/firmware/micropython/ports/unix/modules/cUR/micropython.mk`: firmware cUR usermod makefile.
- `scripts/build/build-micropython.sh`: builds firmware from `krux/firmware/micropython`, stages cUR source.
- `krux/pyproject.toml`: host `uUR` path is `firmware/micropython/lib/cUR`.
- `krux/README.md`: standalone test setup and `UV_PROJECT_ENVIRONMENT` documentation.
- `krux/CONTRIBUTING.md`: test prerequisites and external uv environment.
- `rootfs/micropython-arm32`: current built firmware binary, gitignored but persistent.
- `artifacts/built/cam-yonly/`: stable camera boot image, camd binary/source, zImage, config, logs, checksums.
- `cam/camd.c`: current camera daemon source; differs from exact stable source in artifacts.
- `linux/`: kernel source; camera commits `bc0ddf04a08`, `916fb726a2a`, `cdcc09b3682`.
- `docs/cam-stability-thermal.md`: stable image/test results and preserved artifact paths.
- `docs/build-guide.md`: firmware build path updated to nested MicroPython.
- `krux/src/krux/psbt.py`: top-level `uUR` import; PSBT signing restored.
- `krux/src/krux/pages/seedkeeper.py`: SeedKeeper menus and new-mnemonic flow pending device validation.

## Important Context
- Current main `HEAD`: `4ccf443`; Krux submodule: `d34044cc`; nested MicroPython: `9787a649`; cUR: `ed744562`.
- The nested `micropython` commit `9787a649` may still need pushing to its remote; fresh clones require all nested commits available.
- Main `.gitmodules` has only `linux` and `krux`; MicroPython/cUR are nested under Krux.
- Host tests: `cd krux && uv sync --frozen && uv run poe test-simple`; no full MicroPython firmware build needed.
- External environment: `export UV_PROJECT_ENVIRONMENT=/tmp/opencode/krux-venv` before both `uv sync` and `uv run`.
- Firmware build: `scripts/build/build-micropython.sh`; it stages cUR under the MicroPython build tree and removes the stage afterward.
- Stable camera artifacts: `artifacts/built/cam-yonly/boot.img`, `camd`, `camd.c`, `zImage-dtb`, `kernel.config`, `logs/`, `SHA256SUMS`.
- Stable image and daemon are TWRP/test artifacts; final init-stub/ramdisk and automatic camd startup are still pending.
- `artifacts/built/` is gitignored; survives host reboot but not a future `git clean -fdx`.
- `Kboot` and `MaixPy` are deinitialized; `firmware/Kboot` cached data was removed.
- `firmware/font` is a normal tracked directory, used by simulator/font tooling only.
- Thermal/GRBL printer paths still reference `fpioa_manager`/K210 UART pinmux and are not available on Nexus firmware.
- `/tmp/opencode` was 6.6 GB; only build trees, experimental binaries, ad-hoc `uv`/pydeps, screenshots, and profiling logs remain disposable.

## 2026-11-04 23:07:49 UTC - session compaction (manual)

* session: `ses_e7aba84c4ffewu0RUZojGclCWS`

## Objective
- Maintain the Nexus Krux fork with upstream parity and a standalone host test suite, and validate the camera/QR flows on the real Nexus 5.
- Current focus: make Datum, descriptor, and text-mnemonic QR scans work; compact SeedQR already works.

## Requirements
- No symlinks in the Krux tree; MicroPython and cUR remain nested under Krux/MicroPython.
- Reversible `fastboot boot`; keep camera testing to one clean boot/run cycle when required.
- No modem, Wi-Fi, BT, or vendor HAL.
- Preserve a stable host test suite that does not require a firmware build.
- User has one screen; tell them before showing a QR and allow a few seconds.
- User does not want the camera flash/torch used for screen QR scanning.
- Datum/descriptor/mnemonic camera scans must work, not just compact SeedQR.

## Decisions
- Use VGA 640×480 capture for dense QRs; capture is a 1:1 center crop from the binned sensor (~2.5× angular resolution).
- Keep camd alive across pages; do not stop/restart it on normal camera exit.
- Preview is on-demand/still-only because continuous VGA panel updates freeze/tear.
- Camera page controls: TOUCH/ENTER refresh still preview; VOL+/VOL− exit; flash stays off.
- One camera session per cold boot when the CPP stalls; service restart is insufficient.
- Keep camd supervisor in init-stub and the frame-copy-before-socket-write fix.
- Set MicroPython heap to 8M for full UI plus VGA camera frames.

## Work State

### Completed
- Fork/cUR/MicroPython restructuring, standalone `uv` tests, UR support, and 8M heap are in place.
- Host suite previously green: `1433 passed, 1 xfailed`; not rerun after latest camera edits.
- CompactSeedQR (16-byte) decoded and imported successfully on the device.
- Synthetic Datum QR at 640×480 decodes correctly with the native decoder.
- Camera daemon supervisor, cold-boot recovery, VGA capture, center crop, and socket frame-copy fix implemented.
- QR payload fixes: positional `find_qrcodes()` call and compact-SeedQR raw-byte fallback for missing `latin-1`.
- Torch removed from camera scanning; flash forced off.
- Camera page now supports TOUCH/ENTER still refresh and VOL+/VOL− exit.
- All camera changes committed locally; not pushed.

### Active
- Diagnose why real-camera Datum, descriptor, and text/12-word mnemonic QRs fail while compact SeedQR succeeds.
- Validate the latest touch-refresh/volume-exit controls after a fresh boot.
- Inspect `/data/krux/last-qr.rgb565` from failed scans to determine whether dense QRs are unreadable or `find_qrcodes()` rejects them.
- Preview artifacts/freezes remain unresolved; preview is currently on-demand still.

### Blocked
- Dense QR scans fail despite VGA/center-crop capture.
- Camera stream can stall with `CFG: Resource temporarily unavailable`, `no frame after 5s`, `Camera snapshot failed: no new frame from camd`, requiring a cold boot.
- Repeated camera sessions are unreliable; one session per boot is currently required.
- Device state is uncertain after the last user-forced fastboot; verify whether it is in fastboot, stub/Krux, or Android.
- Real-device testing depends on user availability; SeedKeeper/UR device validation remains deferred.

## Next Move
1. Check `adb devices`/`fastboot devices`; boot `artifacts/built/boot-cam-stub.img` if needed and confirm one camd process.
2. Cold-boot, open camera once, and test TOUCH still refresh plus VOL+/VOL− exit.
3. Attempt Datum in that clean session; on failure pull `/data/krux/last-qr.rgb565` and inspect/decode it on the host.
4. Compare the raw failed frame against the synthetic Datum frame to separate aim/focus/contrast from decoder rejection.
5. If dense QRs remain unreadable, evaluate a tighter crop/zoom, QR preprocessing, or scan-only mode; reboot between descriptor/mnemonic tests.
6. Re-run host tests and push the local commits when camera testing stabilizes.

## Relevant Files
- `krux/src/krux/pages/qr_capture.py`: camera loop, still preview, touch/volume controls, failed-frame dump.
- `cam/camd.c`: VGA center crop, frame-copy socket fix, camd commands.
- `krux/src/sensor.py`: 640×480 protocol, 8s snapshot deadline, camd lifetime.
- `krux/src/krux/camera.py`: auto-exposure tuning, keeps stream alive on hammerhead.
- `krux/src/krux/display.py`: VGA preview rendering.
- `rootfs/init-stub.c`: 8M heap and camd supervisor.
- `krux/firmware/micropython/ports/unix/lcd_bcm.c`: preview commit guard/display support.
- `artifacts/built/boot-cam-stub.img`: current boot image with supervisor/heap.
- `artifacts/built/cam-yonly/camd-vga`: built VGA camd binary deployed to `/data/krux/camd`.
- `artifacts/built/cam-yonly/qr-compact-seed.png`: working CompactSeedQR.
- `artifacts/built/cam-yonly/qr-datum.png`: enlarged Datum test QR.
- `artifacts/built/cam-yonly/qr-descriptor.png`: enlarged descriptor test QR.
- `artifacts/built/cam-yonly/qr-12-word.png`: dense text mnemonic QR that still fails.
- `krux/src/krux/pages/mnemonic_loader.py`: compact-SeedQR payload fallback.
- `/data/krux/last-qr.rgb565`: raw VGA frame from the last failed scan.

## Important Context
- Compact SeedQR works; Datum, descriptor, and text/12-word mnemonic do **not**.
- User corrected an earlier statement: Datum has never worked end-to-end.
- Preview artifacts (dark vertical bars, flicker, partial frames) are display-side; raw VGA capture was stable in an 80-frame test, and synthetic Datum decodes.
- Current camera session can stall after several seconds with `Camera snapshot failed: no new frame from camd`; cold boot is the only reliable recovery.
- Camera controls currently: TOUCH/ENTER = still refresh; VOL+/VOL− = exit; no torch.
- Current commits: main `ee76d7f`, Krux `90e2d31f`, nested MicroPython `b3d562edc`.
- All commits are local/unpushed; only `docs/compaction-log.md` is modified and uncommitted.
- `/data/krux/camd` is the VGA crop build; `artifacts/built/boot-cam-stub.img` is current; `/data/krux/micropython` is current.
- If the camera stream stalls, do not keep tapping; exit with a volume key and cold-boot before the next camera test.

## 2026-11-05 06:23:31 UTC - session compaction (auto)

* session: `ses_e7aba84c4ffewu0RUZojGclCWS`

## Objective
- Make the Nexus 5 camera work in the Krux port for QR/Datum/descriptor/mnemonic scanning, animated UR, and entropy collection, while keeping upstream parity and the standalone host test suite.
- Recently also: implement autofocus on camera start, and document the camera findings.

## Requirements
- No modem, Wi-Fi, BT, or vendor HAL; no symlinks in the Krux tree.
- Reversible `fastboot boot` only; one clean camera session per cold boot when required.
- Never aim or start a camera scan without asking first; give a 10-second cue to switch the screen to the QR, scan ~20 s, then tell the user to return.
- Do not use the camera flash/LED for screen QR scanning.
- Keep QR parsing inside Krux (do not move QR decoding into camd) because many QR variants and other camera uses must be supported.
- Higher scan resolution slows things down and hurts entropy/animated UR; keep the delivered frame modest (Android port uses 640 px).
- Build with the project's hard-float `arm-linux-gnueabihf` toolchain and ccache; build MicroPython out of tree in `/tmp/opencode/mp-build`.
- Kernel work stays in this project (`linux`), preserving KernelSU patches.
- Memory-wipe work is deferred.

## Decisions
- Use the vendor HAL's CPP model: offline processing (`frame_type=0`) with native ION buffers (`native_buff=1`, input fd per frame) instead of realtime.
- Publish only CPP-processed frames (identified by a populated chroma plane) because the ISP also completes raw frames into the same queue.
- Preview is portrait 720×960, rotated in camd with an unsharp mask; no crop (a 3:4 crop of a 4:3 sensor stretches the image).
- Exposure: VMAX raised to the vendor value `0x09d6`; exposure is direct (larger = brighter); bounded mean-driven AE with clipping backoff; continuous AE removed.
- Autofocus uses the captured DW9714 table and the HAL's call sequence, bisectable via `/data/krux/af` (0–4) and a fixed step via `/data/krux/focus`.
- Lens control is currently opt-in (default off) so the camera stays usable.

## Work State
### Completed
- Hard-float + ccache out-of-tree MicroPython build; script verifies VFP ABI. Soft-float binary was the cause of an earlier SIGSEGV.
- Captured the vendor CPP config with a kernel hook (`cpp_frm_NNN.bin`) and wrote `tools/parse_cpp_dump.py` (`--summary`, `--diff`).
- camd now uses the offline/native CPP model; frames are consistent (10-frame spread 0) and geometrically correct (full raw frame verified).
- Fixed tearing: `latest_y` written under `frame_mutex`.
- Fixed tiled preview: `from_nv12` was hard-coded to 640×480 while data was 480×640.
- Portrait preview, correct aspect, unsharp mask; 8-buffer ring fixed stream starvation.
- Exposure verified working (`0x0010`→luma 11, `0x04d0`→129, `0x09c0`→157); AE converges in lit scenes.
- Captured the HAL actuator sequence and DW9714 table (45 steps, 9 codes/step, 10-bit DAC, initial code 169, init regs 0x00ec/0x00a1/0x00f2/0x00dc); `tools/parse_actuator_dump.py` written.
- camd replays `open → GET_INFO → SET_INFO → SET_DEFAULT_FOCUS(7) → MOVE` with the correct `dir`/`sign` convention.
- Boot image size solved: pruned `terminfo` (6.6 MB) and `python3.10` (3.2 MB), hard-float MicroPython, stripped camd → bundled image 22.65 MB (limit 23.07 MB).
- Docs updated: `docs/cam-plan.md`, `docs/cam-state.md`, `docs/cam-frame-path.md`, `docs/cam-integration.md`.

### Active
- Fix the actuator/pipeline interaction: `SET_INFO` and real lens moves stall the stream (level 1 `open+GET_INFO` survives; level 2 stalls after ~2 frames).

### Blocked
- Focus calibration needs a properly lit scene (device is in a dark environment for hours).
- Dense QR/UR/entropy end-to-end validation not done; QR still does not decode.

## Next Move
1. Ensure the boot image carries the newest camd (last run appeared to use an older binary: driver log showed `cfg 1 → cfg 3`, no `cfg 2`).
2. Test `/data/krux/af=2` (SET_INFO only) and `af=4` (single move at power-up); if it stalls, check the `actuator init:` kernel log for parsed `total_steps`/`region_size` vs the captured table.
3. With light: sweep `/data/krux/focus` (e.g. 7/15/25/35) at the working distance, pick the sharpest, then implement startup AF using the HAL sequence.
4. Validate QR, then animated UR and entropy, decoding every Nth frame and keeping delivery at 640–720 px.
5. Re-run the host suite and consider pushing the local commits.

## Relevant Files
- `cam/camd.c`: capture loop, CPP offline/native, publisher filter, portrait downscale, AE hooks, actuator sequence and `af_level` bisect.
- `cam/cam_actuator.h`: DW9714 step constants.
- `cam/cam_chromatrix.h` / `cam/cam_chromatrix_offline.h`: 266-word (1280×960) vs 293-word (1600×1200) messages.
- `krux/src/krux/camera.py`: VMAX/exposure range, AE controller, lens-control flag.
- `krux/src/krux/pages/qr_capture.py`: camera page, preview redraw, snapshot retry.
- `krux/src/sensor.py`: 720×960 socket protocol, luma-only delivery, `gc.collect()`.
- `krux/src/krux/display.py`: hammerhead preview draw (scale 1).
- `linux/drivers/media/platform/msm/camera_v2/sensor/actuator/msm_actuator.c`: actuator dump/log hooks.
- `linux/drivers/media/platform/msm/camera_v2/pproc/cpp/msm_cpp.c`: CPP frame-config dump hook.
- `rootfs/build-stub-ramdisk.sh`: terminfo/python pruning, bundled firmware.
- `rootfs/make-boot-image.sh`: stub image packing.
- `tools/parse_cpp_dump.py`, `tools/parse_actuator_dump.py`: capture parsers.
- `docs/cam-plan.md`, `docs/cam-state.md`: updated status and plan.
- `scripts/build/build-micropython.sh`: hard-float, ccache, out-of-tree, ABI check.

## Important Context
- Device is currently in Android; nothing flashed; all work committed locally (outer `8e53781`, linux `17774b5abd9`, krux commits) and unpushed.
- Camera boot: `fastboot boot artifacts/built/boot-cam-stub.img` (bundled) or `/tmp/opencode/boot-af*.img`; killing camd leaves `/dev/video0` busy until a cold boot; `ctl.stop`/`ctl.start` of the service has crashed/dropped adb repeatedly.
- The bundled boot image restores its own camd to `/opt/krux` and `/data/krux`, so a new camd must be baked into the image.
- Actuator is `/dev/v4l-subdev7` (`msm_actuator`); HAL identity `0x10002`; driver errors `i2c table index out of bound` come from a real lens move.
- The Nexus 5 lens is fixed-focus/VCM parked near infinity; Android (CameraX, 640 px analysis) reads the same QR, so focus — not resolution — is the limiter.
- Touch injection works only when ABS_MT_POSITION_X/Y precede ABS_MT_TRACKING_ID; camera page taps: main menu y=180, submenu y=280; exit tap anywhere.
- Memory-wipe research is in `docs/memory-wipe-findings.md` (deferred).

## 2026-11-05 20:03:10 UTC - session compaction (auto)

* session: `ses_e7aba84c4ffewu0RUZojGclCWS`

## Objective
- Make the Nexus 5 camera reliable in the Krux port for compact/medium/high-density QR, animated UR/BBQR, PTSB/Datum/descriptor/mnemonic scanning, and entropy.
- Memory-wipe work is paused until a serial console is available.

## Requirements
- No modem, Wi-Fi, BT, or vendor HAL; no symlinks in the Krux tree.
- Reversible `fastboot boot` only; never flash; cold boot when the camera pipeline needs a clean session.
- Never aim or start a camera scan without asking; give a 10-second cue, scan ~20 s, then tell the user to return.
- Do not use the camera flash/LED for screen QR scanning.
- Keep QR parsing inside Krux.
- Keep the delivered frame modest; 720×960 is current, 1640×1232 is the source.
- Build with hard-float `arm-linux-gnueabihf` + ccache; MicroPython out of tree in `/tmp/opencode/mp-build`.
- Kernel work stays in this project (`linux`) and must preserve KernelSU.
- Memory-wipe must use a separate linux branch; paused until serial console.
- High-density QR must work without manual exposure where possible; AE must not overblow or drift.
- Preview should be stable, usable for aiming, and not tear.
- Consequent scans should not silently reuse a stale focus; refocus at scan start is acceptable.

## Decisions
- AF stall root cause was `damping_step=0`; `damping_step=1` is required.
- Default is `af=2` with no fixed `focus`; `af=4` + `/data/krux/focus` remains for a fast fixed step.
- `refocus` at the start of every scan; `R` socket command re-runs the AF sweep.
- Power now dumps the current frames (`D` command) instead of refocusing, for HD analysis.
- AE is one-shot at camera start; manual exposure via Vol+/− and on-screen −/+; `_ae_manual` locks it.
- Exposure is direct: larger = brighter. Vol+ now adds `0x100`; Vol− subtracts `0x100`.
- `/data/krux/luma_gain` (1–4) and `/data/krux/unsharp` (0/1/2) are read at camd startup; current test values are `luma_gain=1`, `unsharp=0`.
- quirc supports version 40; `K_QUIRC_MAX_PROCESS_VERSION 40`, 1024 regions, 32768 flood-fill stack.
- The 720×960 preview/decoder path is kept; the full-res `H` snapshot path is disabled because it backed up the CPP queue.
- XZ initramfs (`CONFIG_RD_XZ=y`) is the default; gzip remains available with `COMPRESS=gzip`.
- Memory-wipe kernel mode (`wipe=test`, `wipe=1`) is verified; kexec handoff is blocked and paused.
- SeedKeeper is a top-level “SeedKeeper Card” item plus “Load Seed from Card”; the “Via SeedKeeper Card” item is the fifth New Mnemonic entry.

## Work State

### Completed
- Build outputs now go to `artifacts/built/`; `rootfs/stock/` is ignored; the images were never tracked in git.
- Hard-float MicroPython + ccache out-of-tree build; 32 MiB firmware heap fixes the 1,382,400-byte preview `MemoryError`.
- Ramdisk pruning: terminfo/python already removed; `adbd`/linker/libc/libdexfile stripped; keystore/ubsan removed; duplicate CPP firmware removed; kexec opt-in.
- XZ initramfs: ramdisk ~8.7 MB; camera boot image ~15.95 MB; ~7.1 MB headroom.
- Offline/native CPP path, consistent frames, tearing fix, portrait 720×960 preview, exposure control.
- DW9714 table and HAL sequence captured; AF sweep works; QR decoded at fixed step 21 at 40 cm.
- Base32 fallback added and verified; BBQR deflate now tries AUTO→RAW→ZLIB and encodes ZLIB; 20 KB/33-part synthetic decode passed.
- Manual exposure, exposure direction, per-scan refocus, Power frame dump, and `luma_gain`/`unsharp` controls implemented.
- quirc v40 support added; high-density version limit no longer the blocker.
- SeedKeeper investigation: no regression; deployed `login.py:104` contains “Via SeedKeeper Card” under New Mnemonic.
- Memory-wipe kernel mode verified: `wipe=test` wipes 1487 MB and boots; `wipe=1` wipes and powers off; kexec tooling/docs committed on `airgap/memory-wipe`.

### Active
- High-density QR still fails; compact/medium work, HD only sometimes/never depending on path.
- Frame analysis: `scan2.raw` (720×960) is dark, mean ~15.3, max 55; `scan2_full.raw` is only 786,336 bytes (expected 2,020,480 for 1640×1232), so the dump is partial/truncated.
- Preview tearing observed; not yet determined whether it is the actual frame or LCD preview.
- Camera reuse between PTSB loads still reuses the stream; refocus at scan start is in place, but a fresh sensor start is not.
- `boot-cam-dump3.img` (Power-only atomic dump) is built but not booted or tested.

### Blocked
- Memory-wipe kexec handoff hangs/resets at the bootloader logo; needs a serial console on `ttyHSL0` or working early logs.
- HD QR may be limited by 720×960 decode resolution, focus, or contrast; not yet isolated.
- The `scan_full` dump is truncated; current frame analysis is inconclusive.

## Next Move
1. Boot `/tmp/opencode/boot-cam-dump3.img`; keep `af=2`, no `focus`, `luma_gain=1`, `unsharp=0`.
2. Ask the user to hold the HD PTSB QR in the PTSB/Datum Tool camera and press Power once; pull `/data/krux/scan.raw` and `/data/krux/scan_full.raw`.
3. Decode both frames with `/tmp/opencode/venv/bin/python` and pyzbar at rotations 0/90/180/270; compare 720×960 vs 1640×1232.
4. Inspect the frames for tearing, partial strips, contrast, and QR module size; determine whether the tear is in the frame or only the LCD preview.
5. If the 720 frame is the limit, design a full-resolution decode path that does not share the capture/CPP thread; otherwise tune focus/exposure.
6. Re-test HD with `luma_gain=2` and `unsharp` 0/1/2 if the raw frame is too dark.
7. Decide whether to stop the sensor on camera-page exit for a truly fresh start, or keep stream reuse plus per-scan refocus.
8. Re-run the host test suite; commit the uncommitted atomic-dump change in `cam/camd.c`; push local commits when appropriate.

## Relevant Files
- `cam/camd.c`: capture/CPP, AE/AF, refocus, dump, luma gain/unsharp; atomic dump change is uncommitted.
- `krux/src/krux/pages/qr_capture.py`: preview loop, manual exposure, refocus/dump keys, frame filtering.
- `krux/src/krux/camera.py`: `_auto_expose`, `adjust_exposure`, `refocus`, `dump`.
- `krux/src/sensor.py`: camd socket, `snapshot`, `refocus`, `dump`.
- `krux/src/krux/bbqr.py`, `krux/src/krux/_base32.py`, `krux/src/krux/base32_compat.py`: BBQR/base32/deflate.
- `krux/firmware/micropython/ports/unix/k_quirc_internal.h`, `k_quirc_version.c`: quirc v40/regions/stack.
- `krux/firmware/micropython/ports/unix/image_maixpy.c`: `get_statistics_roi`.
- `krux/src/image.py`: native image wrapper.
- `rootfs/build-stub-ramdisk.sh`, `rootfs/make-boot-image.sh`, `rootfs/init-stub.c`: ramdisk, XZ, heap, image packing.
- `linux/arch/arm/configs/lineageos_hammerhead_defconfig`: `CONFIG_RD_XZ`, camera branch; memory-wipe branch has `CONFIG_KEXEC`/`CONFIG_MEMORY_WIPE`.
- `docs/cam-autofocus.md`, `docs/cam-frame-path.md`, `docs/cam-state.md`: camera state and findings.
- `docs/memory-wipe-state.md`, `docs/memory-wipe-continue.md`, `docs/linux34_kexec_wipe_plan.md`: memory-wipe pause, continuation prompt, plan.
- `scripts/build/build-micropython.sh`, `scripts/build/build-kexec.sh`: build tooling.
- `tools/parse_cpp_dump.py`, `tools/parse_actuator_dump.py`: captured vendor data parsers.
- `artifacts/built/`: ignored ramdisks, boot images, `kexec-arm`; `/tmp/opencode/boot-cam-dump3.img` built but not booted.

## Important Context
- Current device is in the stub with `boot-cam-dump2.img`; `af=2`, no `focus`, `luma_gain=1`, `unsharp=0`; Power dumps frames.
- `scan2.raw` is 691,200 bytes (720×960); `scan2_full.raw` is 786,336 bytes, but the source is 1640×1232 = 2,020,480 bytes, so the full dump is truncated.
- The previous `scan2.raw` image is almost black (mean 15.3, max 55); the preview “looks fine” because of LCD backlight/display, not raw luma.
- `boot-cam-dump3.img` writes only on Power and uses `.tmp` + `rename`; it has not been booted.
- Exposure buttons: Vol+ = `+0x100`; Vol− = `−0x100`; on-screen right `+`, left `−`; first adjustment locks AE.
- AF: `af=2`/no `focus` runs the sweep at startup; `refocus` runs at each scan start; Power is now dump, not refocus; `R` is the socket refocus command.
- quirc v40 is present; compact/medium work; HD still fails at 720.
- Full-res `H` path is disabled; the 1640×1232 `src_y` is available only in the dump.
- Camera image size ~15.95 MB; XZ ramdisk ~8.7 MB; boot limit 23,068,672 bytes.
- Memory-wipe branches: `linux:airgap/memory-wipe` head `44e3385803d`; camera linux branch `airgap/cam-ksu-dump` at `17774b5abd9`; outer repo points back to the camera branch.
- `cam/camd.c` atomic dump change is uncommitted; `boot-cam-dump3.img` is built from it.
- SeedKeeper: top-level item and New Mnemonic fifth item are present; the earlier “missing from Load Mnemonic” was a misunderstanding.

## 2026-11-06 01:05:53 UTC - session compaction (manual)

* session: `ses_e7aba84c4ffewu0RUZojGclCWS`

## Objective
- Make the Nexus 5 camera reliable in the Krux port for compact/medium/high-density QR, animated UR/BBQR, PTSB/Datum/descriptor/mnemonic scanning, and entropy.
- Recover the known-good firmware from the original device and make the project tree the source of truth.

## Requirements
- No modem, Wi-Fi, BT, or vendor HAL; no symlinks in the Krux tree.
- Reversible `fastboot boot` only; never flash; cold boot when the camera pipeline needs a clean session.
- Never aim or start a camera scan without asking; give a 10-second cue, scan ~20 s, then tell the user to return.
- Do not use the camera flash/LED for screen QR scanning.
- Keep QR parsing inside Krux.
- Keep the delivered frame modest; 720×960 is current, 1640×1232 is the source.
- Build with hard-float `arm-linux-gnueabihf` + ccache; MicroPython out of tree in `/tmp/opencode/mp-build`.
- Kernel work stays in this project (`linux`) and must preserve KernelSU.
- Memory-wipe must use a separate linux branch; paused until serial console.
- High-density QR must work without manual exposure where possible; AE must not overblow or drift.
- Preview should be stable, usable for aiming, and not tear.
- Consequent scans should not silently reuse a stale focus; refocus at scan start is acceptable.
- Compare only the `.py` files pulled from the device with the project tree; apply only newer/fixed files.
- Deploy Krux + MicroPython to the new device using `deploy-firmware.sh`.
- Code on the device should never be newer than the project tree.

## Decisions
- AF stall root cause was `damping_step=0`; `damping_step=1` is required.
- Default is `af=2` with no fixed `focus`; `af=4` + `/data/krux/focus` remains for a fast fixed step.
- `refocus` at the start of every scan; `R` socket command re-runs the AF sweep.
- Power is a plain full AF sweep; the blocking Power re-expose was removed.
- AE is one-shot at camera start; manual exposure via Vol+/− and on-screen −/+; `_ae_manual` locks it.
- Exposure is direct: larger = brighter; Vol+ adds `0x100`; Vol− subtracts `0x100`.
- `/data/krux/luma_gain` (1–4) and `/data/krux/unsharp` (0/1/2) are read at camd startup; current test values are `luma_gain=1`, `unsharp=0`.
- quirc supports version 40; `K_QUIRC_MAX_PROCESS_VERSION 40`, 1024 regions, 32768 flood-fill stack.
- The 720×960 preview/decoder path is kept; the full-res `H` snapshot path is disabled because it backed up the CPP queue.
- XZ initramfs (`CONFIG_RD_XZ=y`) is the default; gzip remains available with `COMPRESS=gzip`.
- Memory-wipe kernel mode (`wipe=test`, `wipe=1`) is verified; kexec handoff is blocked and paused.
- SeedKeeper is a top-level “SeedKeeper Card” item plus “Load Seed from Card”; the “Via SeedKeeper Card” item is the fifth New Mnemonic entry.
- The CPP debug dump (`cpp_dump_frames=64`) drops ISP frames, burns CPU, and stalls the pipeline; it must be disabled after `STREAMON` and at runtime.
- The AE re-running on every camera page changed exposure and made QR work only once; it should run only on the first camera page.
- The on-demand camd start/stop and per-frame idle skip left the ISP/CPP with empty buffers; boot-time camd start + downscale-only idle skip is the safe path.
- The 293-word offline CPP message is rejected/clamped by this driver (still logs 266); the experiment was reverted.
- The working firmware is the original device’s `/data/krux`, not the bundled `/opt/krux` experimental code.
- `deploy-working-krux.sh` was created to restore the wiped `/data/krux`; the user says deployment should come from the project tree via `deploy-firmware.sh`.

## Work State

### Completed
- Build outputs now go to `artifacts/built/`; `rootfs/stock/` is ignored; images were never tracked in git.
- Hard-float MicroPython + ccache out-of-tree build; 32 MiB firmware heap fixes the 1,382,400-byte preview `MemoryError`.
- Ramdisk pruning; XZ initramfs ~8.7 MB; camera boot image ~15.95 MB; ~7.1 MB headroom.
- Offline/native CPP path, consistent frames, tearing fix, portrait 720×960 preview, exposure control.
- DW9714 table and HAL sequence captured; AF sweep works; QR decoded at fixed step 21 at 40 cm.
- Base32 fallback added and verified; BBQR deflate tries AUTO→RAW→ZLIB and encodes ZLIB; 20 KB/33-part synthetic decode passed.
- Manual exposure, exposure direction, per-scan refocus, Power frame dump, and `luma_gain`/`unsharp` controls implemented.
- quirc v40 support added; high-density version limit is no longer the blocker.
- SeedKeeper investigation: no regression; deployed `login.py:104` contains “Via SeedKeeper Card”.
- Memory-wipe kernel mode verified: `wipe=test` wipes 1487 MB and boots; `wipe=1` wipes and powers off; kexec tooling/docs committed on `airgap/memory-wipe`.
- QR display cut-off fixed by marking `draw_qr_code_binary` in progress and committing the finished code.
- BBQR compression fixed by enabling `MICROPY_PY_DEFLATE_COMPRESS=1`.
- HD QR now decodes near full screen; the AF macro end (step 44) is scored.
- Raw-pass frame filter now publishes only CPP-processed frames; this fixed preview tearing, AE overexposure, and AF scoring.
- AF fine sweep widened to ±8, prefers the fine peak, and approaches the final step with a one-step forward move.
- CPP debug dump disabled at runtime; preview is stable.
- Local ±5 fine AF around the last good step with a full-sweep fallback was implemented.
- Static-link build bug fixed: a comment inside the backslash-continued `make` command produced a dynamic PIE binary; the script now checks for a static binary.
- Working set pulled from original device `/data/krux` to `/tmp/opencode/original-data-krux` and `deploy/working-krux`.
- `krux/src` camera/QR files mirrored from the working set; commits `d1e15529` (krux) and `c7cdcaa` (outer).
- `deploy/working-krux/README.md` and `scripts/deploy-working-krux.sh` created.
- 293-word CPP experiment reverted (`f2ec8eb`).

### Active
- Compare only the pulled `.py` files against `krux/src`; identify which device files are newer or contain fixes; apply only those.
- Explain why `deploy-working-krux.sh` was created and why it was used.
- Deploy Krux + MicroPython to the new device with `deploy-firmware.sh`.
- Re-test camera, AF, AE, repeated QR scans, and idle CPU on the new device.
- Working tree is not clean: `docs/compaction-log.md` modified; `krux` submodule has local changes.

### Blocked
- The test device `0879524b0068abca` currently has `/data/krux` wiped and shows `bytesused=0` / CPP `CFG` failures.
- Memory-wipe kexec handoff hangs/resets at the bootloader logo; needs a serial console on `ttyHSL0` or working early logs.
- HD QR reliability across repeated scans is not yet re-verified with the restored working set.
- Idle CPU remains high (camd ~25%, micropython 25–95% at the menu); the LCD 200 ms commit change is in the experimental build, not the restored working set.

## Next Move
1. Diff only the pulled `.py` files (`/tmp/opencode/original-data-krux` vs `krux/src`) and identify newer/fixed device files.
2. Apply only those newer/fixed files to the working tree; keep the project tree as source of truth.
3. Explain `deploy-working-krux.sh` was a recovery script for the wiped `/data`; deployment should use `deploy-firmware.sh`.
4. Deploy Krux + MicroPython to `0879524b0068abca` using `deploy-firmware.sh`.
5. Test camera/QR/AF/AE and repeated scans on the new device.
6. Address remaining idle CPU and AE/QR-once behavior if reproduced.
7. Re-run host tests and commit/push.

## Relevant Files
- `deploy/working-krux/`: known-good firmware pulled from original device; reference set.
- `/tmp/opencode/original-data-krux/`: raw 68 MB pull of original `/data/krux`.
- `krux/src/sensor.py`, `krux/src/krux/camera.py`, `krux/src/krux/pages/qr_capture.py`, `krux/src/krux/bbqr.py`, `krux/src/krux/baseconv.py`, `krux/src/krux/power.py`: working files mirrored from device.
- `scripts/deploy-working-krux.sh`: recovery deploy script the user questioned.
- `deploy-firmware.sh`: deployment path the user wants used.
- `cam/camd.c`: camera daemon; current source reverted to 266-word message and downscale-only idle.
- `rootfs/init-stub.c`: camd startup, `/data` handling, CPP dump write.
- `scripts/build/build-micropython.sh`: static-link fix and deflate flags.
- `krux/firmware/micropython/ports/unix/lcd_bcm.c`: QR partial-commit guard and 200 ms commit cadence.
- `docs/cam-autofocus.md`, `docs/cam-frame-path.md`, `docs/cam-state.md`, `docs/display-qr.md`: camera/display findings.
- `/tmp/opencode/boot-cam-idle.img`: last known-good boot image on the test device.
- `linux` branch `airgap/cam-ksu-dump`: KernelSU camera kernel; `zImage-dtb` at `/tmp/opencode/ko-cam-xz/arch/arm/boot/zImage-dtb`.
- `artifacts/built/`: ignored ramdisks, boot images, `kexec-arm`.

## Important Context
- Original device: `0643efdcf0eca63a` (Android/stub, working `/data/krux`); test device: `0879524b0068abca` (stub, `/data/krux` wiped).
- Original `/data/krux` is readable via adb in the stub; Nexus 5 has no encryption on this ROM.
- Working set sizes: `sensor.py` 5968, `krux/camera.py` 17597, `krux/display.py` 22128, `krux/bbqr.py` 7978; `sensor.py`/`display.py`/`bbqr.py` match commit `69a3fbda`; `camera.py` differs from `69a3fbda` (17458).
- Current `krux/src` was overwritten with the working camera/QR files; the experimental changes remain in git history.
- The original device’s working code shadows `/opt/krux`; wiping `/data` forced the experimental bundle onto the test device and broke it.
- CPP dump must be disabled at runtime: `echo 0 > /sys/module/msm_cpp/parameters/cpp_dump_frames`.
- On-demand camd stop/start and idle pace sleep both broke the CPP queue; boot-time camd start is the working path.
- The 293-word CPP message is clamped/logged as 266; do not use it.
- `krux` submodule is dirty (`m krux`) and `docs/compaction-log.md` is modified.
- Camera scans: ask the user, give a 10-second cue, scan ~20 s, then tell them to return.

## 2026-11-06 16:15:52 UTC - session compaction (manual)

* session: `ses_e7aba84c4ffewu0RUZojGclCWS`

## Objective
- Make the Nexus 5 camera reliable in the Krux port: QR scanning (compact/medium/high density, static/animated UR/BBQR) and then entropy collection from camera images, with the project tree as the single source of truth.

## Requirements
- Project tree is the source of truth; device code must never be newer than the tree.
- Two deployment targets: `/data/krux` (dev, pushed by `scripts/deploy-firmware.sh`) and `/opt/krux` (bundled in the boot image). Use `/data/krux` exclusively until the project is finalised; do not edit files on the device directly.
- Temporary camera state (e.g. `af`) must live in `/tmp`, be optional, and be reinitialised when missing.
- Do not break the entropy capture while tuning QR scanning.
- Never flash; `fastboot boot` only. Cold boot when the camera pipeline needs a clean session.
- Never aim/start a camera scan without asking; 10-second cue, ~20 s scan, then tell the user to return.
- No modem/Wi-Fi/BT/vendor HAL; no symlinks in the Krux tree.
- Camera scans: ask the user, give a 10-second cue, scan ~20 s, then tell them to return.
- Keep the delivered frame modest; 720×960 is current, 1640×1232 is the source.

## Decisions
- The device's `/data/krux` snapshot was commit `9bfd7c20` (11-04 21:11) — 27 commits behind the project tree, not newer. The previous "restore from device" was a pure regression; reverted (`fc0709ee`).
- `/data/krux` now takes precedence over `/opt/krux`; the dev boot image is built without `--bundle` (one firmware copy only).
- Camera tuning state lives in `/tmp/krux/{af,focus,luma_gain,unsharp}`; camd reads it at startup and writes defaults when missing.
- `af` level 0 = autofocus off (silent failure); default is now 2. `MIN_GAIN=0x0060`, `MAX_GAIN=0x00FF`, `MIN_EXPOSURE=0x0100`, `MAX_EXPOSURE=0x09d6`, `BRIGHT_TARGET=225`, `SATURATED=250`, `SETTLED_TOLERANCE=18`.
- AE measures the bright pixels of the **middle of the frame** (`l_bright_mean` over the ROI, native stat added).
- AE rules: saturated → digital gain off first, then exposure ×0.7, then analog gain; too dark → exposure first, then analog gain; digital gain never used to brighten (it lifts noise with signal and pins the measurement). Digital gain reset to 1 at every scan start.
- Auto-exposure follows the scene continuously (every 3rd frame) and is frozen while the AF sweeps (`hdr[18]` carries the sweep state; `sensor.sweeping()`).
- The AF sweep is deferred: no sweep at page open; a sweep is requested when the code is unread for ≥15 frames, the picture is settled, at most 2 per scan. Tap = full refocus; PWR (== ENTER) leaves the camera; `−`/`+` strip and Vol± adjust exposure.
- The decoder and the statistics now use the sensor's **own luma plane** (kept in the native image object); the RGB565 round trip cost up to 5 levels/pixel and was removed from both paths.
- The picture stays **grey**: the chroma plane is centred on 16, not 128, so it is not NV12 and a YUV conversion turns the picture green. The chroma is read and dropped.
- ZXing/Android reference: `PlanarYUVLuminanceSource` "uses Y channel directly without RGB conversion"; robustness to lighting comes from `HybridBinarizer` (local 8×8 thresholding), not from the camera; ZXing's Android app does no AE of its own.
- The memory-wipe kexec experiment is off master (branches `airgap/memory-wipe` in outer repo, `krux`, `krux/firmware/micropython`; kernel side already on `linux:airgap/memory-wipe`).
- `scripts/deploy-firmware.sh` now verifies the interpreter md5 on the device and fails loudly if it differs.

## Work State
### Completed
- Snapshot analysed: 195 `.py` files; 138 identical to the tree, 1 (`image.py`) newer in the tree, 56 device-only scratch `test_*.py`. `docs/device-snapshot-analysis.md` written.
- Reverted the bogus device "restore"; deleted `deploy/working-krux/` and `scripts/deploy-working-krux.sh`.
- Init stub rebuilt with `/data/krux` precedence; dev image without bundle; `rootfs/stub/krux-init` rebuilt.
- Fixed the ROI-statistics call bug (native `get_statistics_roi`, not the Python wrapper) — the AE had been measuring the whole frame all along.
- Fixed `stats.l_bright_mean` missing `()` (bound-method arithmetic crash) and `self._ae_exposure` read before assignment.
- Added `sys.print_exception` traceback for camera-start failures; replaced the bogus "Let it cool" message with "Try again".
- Added the native `l_bright_mean`/`l_bright_count` statistics; the image object keeps the luma plane.
- `camd` rebuilt from source; `deploy-firmware.sh` rebuilds it when the source is newer; removed the obsolete `tools/ae-patch.py`/`build-ae-camd.sh` (they duplicated the AE block).
- QR scanning works: low/medium density, animated and non-animated; HD QR decoded on the first attempt after the brightness settled, and instantly on repeat scans with a good starting exposure.
- Entropy path isolated: digital gain forced off, manual exposure lock ignored, in `ENTROPY_MODE`.

### Active
- HD QR still sensitive to the exact exposure and takes ~10 s to settle; pointing away at a darker scene sends the exposure to the ceiling and it does not come back down promptly.
- User was asked to re-test HD QR with the true-luma decoder/AE (device running `7d90d5c9…`).

### Blocked
- Chroma format unknown (values centred on 16, not 128) — colour preview cannot be enabled safely without a colourful test scene.
- Memory-wipe kexec handoff still hangs/resets at the bootloader; needs a serial console on `ttyHSL0`.
- Intermittent broken preview (grey bar, odd shapes) seen once; not reproduced since, cause unknown.

## Next Move
1. Wait for the user's HD QR re-test with the true-luma decoder/AE; check `fw.log` `Camera AE mean=…` lines and `camd.log` `[af]` trace.
2. Restore the full-resolution decode path for dense codes (`sensor.snapshot_full()` + camd `H` command, removed in `3b5761d`/`f8cce078`, known code in `16fa1cd`/`2d69a789`): preview is 720×960 from a 1640×1232 source, so a dense code is decoded at ~1/3 of the pixels per module.
3. Consider a ZXing-style local adaptive binariser in the native module (currently quirc thresholds globally — the other half of "detects instantly regardless of lighting").
4. Make the AE converge faster / recover from a dark scene: investigate why pointing away leaves the exposure at the ceiling.
5. Then start the entropy capture task (entropy collection from camera image with preview), using the new AE.
6. Update `docs/cam-state.md` with the final AE/AF design; commit and push everything.

## Relevant Files
- `krux/src/krux/camera.py`: the Camera object, `_stats`, `_level`, `_auto_expose`, `auto_expose_step`, `adjust_exposure`, `picture_settled`, `auto_expose_follow`, `initialize_run`.
- `krux/src/krux/pages/qr_capture.py`: the scan loop — continuous AE every 3rd frame, deferred sweep trigger, tap = refocus, PWR = exit.
- `krux/src/sensor.py`: `snapshot()`, `set_luma_gain()`, `set_exposure_gain()`, `refocus()`, `sweeping()`.
- `krux/src/image.py`: `from_nv12` wrapper, `get_statistics(roi)`.
- `cam/camd.c`: camera daemon — publish gate, AF sweep state machine, `af_best_contrast`, `af_sweeping`, `af_abort`, `state_read`, `luma_gain` command, `H`/full-res path absent.
- `krux/firmware/micropython/ports/unix/image_maixpy.c`: `from_nv12` (grey packing + luma plane), `find_qrcodes`, `get_statistics`, `get_statistics_roi`, `image_luma`.
- `scripts/deploy-firmware.sh`: the deployment path; now verifies the interpreter md5 and rebuilds camd when stale.
- `rootfs/init-stub.c`: firmware dir selection (`/data/krux` first, `/opt/krux` fallback).
- `rootfs/build-stub-ramdisk.sh`, `rootfs/make-boot-image.sh`: ramdisk/boot-image build (no `--bundle` for dev).
- `deploy/device-config/`: `af`, `focus`, `luma_gain`, `unsharp` + README.
- `docs/device-snapshot-analysis.md`: the snapshot-vs-tree analysis and the two extra traps found.
- `docs/cam-state.md`: camera state (needs updating with the final AE/AF design).
- `docs/cam-autofocus.md`, `docs/cam-frame-path.md`: camera findings; the former records the `damping_step=0` stall.
- `/tmp/opencode/look.py`: host-side analysis of a dumped frame (luma/ROI means, clipped counts, brightness map).
- `/tmp/opencode/bootonly.sh`: reboot-to-stub helper (`adb reboot bootloader` → `fastboot boot /tmp/opencode/boot-dev.img`).

## Important Context
- Devices: original `0643efdcf0eca63a` (Android, working `/data/krux`), test `0879524b0068abca` (stub). Boot image in use: `/tmp/opencode/boot-dev.img` (built from `artifacts/built/ramdisk-stub` + `/tmp/opencode/ko-cam-xz/arch/arm/boot/zImage-dtb`).
- `fastboot boot` interrupted by a tool-call abort leaves the device in Android — run boot steps detached (`setsid nohup /tmp/opencode/bootonly.sh &`) and never interrupt them.
- Dump helper: `adb push /tmp/opencode/dumpctl.py /tmp/krux-dump.py` then `adb shell '/data/krux/micropython /tmp/krux-dump.py'`; camd `D` command writes `/data/krux/scan.raw` (luma) and `/data/krux/scan_uv.raw` (chroma).
- `luma_gain`/`unsharp`/`af`/`focus` are read by camd at startup from `/tmp/krux/`; the `G` socket command sets the luma gain live.
- The AE's bright mean is the mean of the pixels within 40 levels of the frame's brightest pixel — a few brighter outliers can pin it, which is why the ROI is used.
- Measured: luma→RGB565→luma costs mean 2.5 / max 5 levels, 51 % of pixels off by >2; the delivered chroma plane is centred on 16 (mode 16, range 9–45).
- The device's frame is noisy in dim scenes (pixel values jumping 23→27→80→43→24) — the likely remaining cause of dense-code contrast loss, not a downscale fault.
- Commits: outer repo `90722ba`…`6ce05fe` (see `git log`), `krux` `fc0709ee`…`653d52a9`, micropython `017f22065`…`cd318f145`. Nothing is pushed yet (outer repo is ahead of origin/master by ~134 commits; `krux` ahead of `eugenesan/nexus-port` by ~81).
- The entropy capture measures Shannon entropy over `img.to_bytes()` and the L/A/B channel spread — so any change to the delivered picture changes the entropy measurement; the digital gain is therefore forced off for it.
- `docs/compaction-log.md` is modified but uncommitted; `krux` submodule pointer updates must be committed in the submodule first, then in the outer repo (use absolute paths — `cd ..` inside the submodule lands in the wrong repo).

## 2026-11-09 01:48:45 UTC - session compaction (manual)

* session: `ses_e7aba84c4ffewu0RUZojGclCWS`

## Objective
- Make the Nexus 5 camera reliable in the Krux port: QR scanning (compact/medium/high density, static/animated UR/BBQR) and then entropy collection from camera images, with the project tree as the single source of truth.

## Requirements
- Project tree is the source of truth; device code must never be newer than the tree.
- Two deployment targets: `/data/krux` (dev, pushed by `scripts/deploy-firmware.sh`) and `/opt/krux` (bundled in the boot image). Use `/data/krux` exclusively until finalised; do not edit files on the device directly.
- Temporary camera state (e.g. `af`) must live in `/tmp`, be optional, and be reinitialised when missing.
- Do not break the entropy capture while tuning QR scanning.
- Never flash; `fastboot boot` only. Cold boot when the camera pipeline needs a clean session.
- Never aim/start a camera scan without asking; 10-second cue, ~20 s scan, then tell the user to return.
- No modem/Wi-Fi/BT/vendor HAL; no symlinks in the Krux tree.
- Keep the delivered frame modest; 720×960 is current, 1640×1232 is the source.
- Micropython heap can be enlarged freely (device has 2 GB); firmware launches with `-X heapsize=32M`.

## Decisions
- `/data/krux` takes precedence over `/opt/krux`; dev boot image built without `--bundle`.
- Camera tuning state lives in `/tmp/krux/{af,focus,luma_gain,unsharp}`; camd reads it at startup and writes defaults when missing.
- `af` level 0 = autofocus off; default is 2. `MIN_GAIN=0x0060`, `MAX_GAIN=0x00ff`, `MIN_EXPOSURE=0x0100`, `MAX_EXPOSURE=0x09d6`, `BRIGHT_TARGET=225`, `SATURATED=250`, `SETTLED_TOLERANCE=18`.
- AE measures the bright pixels of the middle of the frame (`l_bright_mean` over the ROI, native stat).
- AE rules: saturated → digital gain off first, then exposure ×0.7, then analog gain; too dark → exposure first, then analog gain; digital gain never used to brighten.
- Auto-exposure follows the scene every 3rd frame and is frozen while AF sweeps.
- AF sweep deferred: no sweep at page open; requested when unread ≥15 frames, picture settled, max 2 per scan. Tap = full refocus; PWR (== ENTER) exits; `−`/`+` strip and Vol± adjust exposure.
- Decoder and statistics use the sensor's own luma plane; RGB565 round trip removed (cost up to 5 levels/pixel).
- Picture stays grey: chroma plane is centred on 16, not 128, so it is not NV12; a YUV conversion turns the picture green.
- **Field of view is NOT cropped** — proven against the Android camera on the same device/scene; our FOV is the same (phone body covers 87% Android / 84% ours). Kernel reports zero CAMIF geometry errors; camd's downscale reconstructs the preview with correlation 0.979.
- **The framing problem was aim + distance**, not optics: phone was ~3.5× further away than in the Android reference photo.
- **The real decode blocker is the illumination gradient together with the screen moiré**; each alone is survivable.
- The stub ramdisk must be **gzip**, not xz — the stock bootloader silently boots nothing with xz and the device falls back to Android.
- Plan reordered: exposure ceiling first (baseline otherwise measured through a broken controller); Step 3 becomes 3a remove 2-pixel artefact, 3b flatten illumination, 3c threshold modes.

## Work State
### Completed
- Reverted bogus device "restore"; deleted `deploy/working-krux/` and `scripts/deploy-working-krux.sh`.
- Init stub rebuilt with `/data/krux` precedence; dev image without bundle.
- Fixed ROI-statistics call bug (native `get_statistics_roi`), `stats.l_bright_mean` missing `()`, `self._ae_exposure` read before assignment; added tracebacks.
- `camd` rebuilt from source; `deploy-firmware.sh` rebuilds when stale and verifies the interpreter md5.
- QR scanning works for low/medium density, animated and non-animated.
- `docs/cam-ae-af-brief.md` written (SoC corrected to MSM8974); review doc `docs/cam-bringup-af_ae_qr-review.md` committed; plan `docs/cam-ae-af-plan.md` written and twice updated.
- `docs/cam-corpus-findings.md` written and updated three times.
- Host harness `tools/quirc-replay` built and verified; both local-threshold paths compile (`K_QUIRC_BILINEAR_THRESHOLD`, `K_QUIRC_ADAPTIVE_THRESHOLD`).
- Tablet display chain `tools/show-qr` built, pixel-exact and self-verifying (`"verified": true`, 6 px/module measured back from the tablet's screenshot).
- Tools built: `tools/capture-one.py`, `tools/capture-corpus`, `tools/corpus-capture`, `tools/corpus-try`, `tools/frame-try`, `tools/moire-test`, `tools/locate-qr`, `tools/make-qr-targets`, `tools/send-key`, `tools/boot-stub`, `tools/measure-fps`, `tools/measure-period`, `tools/measure-rate`, `tools/preview-vs-source`, `tools/decode-qr`, `tools/decode-frame`, `tools/code-orientation`, `tools/aim-check`, `tools/aim-now`.
- Paper QR identified as **version 17** (85×85 modules), 6.0 source px/module, 2.63 preview px/module.
- Sensor's 2-pixel artefact characterised: even/odd bias, 33% of variance on blank paper; notch restores finder detection at full resolution.
- Moiré/gradient experiment done: only gradient+moiré together break the decode; gradient must fall from 5.8× to ~1.1×.
- Android reference photos taken; field-of-view question settled; whole chain proven (Android code decodes with our decoder at 809×815 and at 541×545 = 5.1 px/module).
- First corpus capture: 20 frames of `v20-dense` across the exposure ladder at two gains, exposure writes sticking; finders detected at 8.14 px/module in the good frames.

### Active
- Second corpus capture: waiting for the phone to be moved ~3.5× closer and aimed at the code (currently 0.68 px/module, code at the right edge).

### Blocked
- Chroma format unknown (values centred on 16) — colour preview unsafe without a colourful test scene.
- Memory-wipe kexec handoff hangs at the bootloader; needs a serial console on `ttyHSL0`.
- Intermittent broken preview (grey bar, odd shapes) seen once; not reproduced.

## Next Move
1. Ask the user to move the phone ~3.5× closer and centre the code; they can iterate with `./tools/aim-now` (target ≥5 px/module, whole code in frame).
2. Optionally offer to re-display the code larger on the tablet (filling the screen width instead of 630/1280 px) to gain 1.6× px/module.
3. Re-run the corpus sweep (`/tmp/sweep.sh` pattern, or `tools/corpus-capture`) once framing is good; capture preview + source per exposure.
4. Run `tools/decode-frame` / `tools/corpus-try` offline over the corpus: notch (axis + diagonal), flat-field, then threshold modes.
5. Implement the exposure-ceiling clamp in `camd` (`VMAX-4`, echo applied value back) plus anti-windup.
6. Fix `K_QUIRC_MAX_BITMAP` 1954 → 3917 (versions 28–40).
7. Then full-resolution crop-and-decode, percentile AE, AF scheduling, entropy capture.

## Relevant Files
- `docs/cam-corpus-findings.md`: the consolidated findings — FOV test, paper QR, gradient+moiré result, corpus status.
- `docs/cam-ae-af-plan.md`: the ordered plan with the revised Step 3 and the two preconditions for a valid capture.
- `docs/cam-bringup-af_ae_qr-review.md`: the external review being followed.
- `krux/src/krux/camera.py`: AE (`auto_expose_step`, `_stats`, `_level`, `picture_settled`), `MAX_EXPOSURE` at line 46.
- `krux/src/krux/pages/qr_capture.py`: the scan loop, AE every 3rd frame, deferred sweep trigger.
- `cam/camd.c`: daemon — downscale, publish gate, AF sweep, `E`/`V`/`D` socket commands, ISP-IF crop.
- `krux/firmware/micropython/ports/unix/image_maixpy.c`: `from_nv12` (grey packing + luma plane), `find_qrcodes`, `get_statistics_roi`.
- `krux/firmware/micropython/ports/unix/k_quirc.h`: `K_QUIRC_MAX_BITMAP 1954` (version-27 ceiling).
- `krux/firmware/micropython/ports/unix/k_quirc.c`: `K_QUIRC_MAX_IMAGE_DIM 1280` (blocks the 1640×1232 source).
- `rootfs/build-stub-ramdisk.sh`: now defaults to `COMPRESS=gzip`.
- `tools/aim-now`, `tools/aim-check`: live aiming feedback.
- `tools/decode-frame`, `tools/corpus-try`, `tools/moire-test`: offline correction experiments.
- `tools/show-qr`: verified tablet display.
- `tools/boot-stub`: reboot-to-stub helper (device returns to Android after any reboot).
- `tools/send-key`: ENTER = KEY_POWER on `/dev/input/event0`; closes the camera page.

## Important Context
- Devices: original `0643efdcf0eca63a` (Android, not connected), test `0879524b0068abca` (stub). Tablet: `41036f02470031ff` (GT-N5110, 800×1280, Android 4.4.2, ~188.7 PPI).
- Boot image rebuilt at `/tmp/boot-dev.img` from `artifacts/built/kernel-stub` + `artifacts/built/ramdisk-stub` (gzip) via `rootfs/make-boot-image.sh`.
- The camera page in Krux **must be closed** before any capture: while open, the firmware AE writes exposure every 3rd frame and overrides the capture script. Verify in `camd.log`: last line should be `[sensor] E exp=0x.... gain=0x.... (ctl)`.
- Tablet sleep fix: `settings put global stay_on_while_plugged_in 3`, `screensaver_enabled 0`, `screen_off_timeout 1800000`; wake with power key + corner tap (centre tap closes the gallery).
- Android camera reference (`/tmp/opencode/android_ref.jpg`) is 2448×3264, code at 7.70 px/module; the control that proves the decoder works.
- Sensor read-back: `0x0340/0x0341 = 0x04e4` (VMAX 1252), `0x0342/0x0343 = 0x0d70` (HMAX 3440, full-mode value); exposure must be ≤ VMAX−4 = 0x04e0.
- The tablet shows codes at 9×9 cm to 11×11 cm physically; computer QRs are ~9–11 cm, phone QRs ~4–5 cm.
- Backgrounds on computers/phones are usually dark/black — a dark surround actually helps quirc's flood fill.
- Commits this session: outer repo `9315e8d`…`e565c11`; `krux` `653d52a9`; micropython `cd318f145`. Nothing pushed.
- `docs/compaction-log.md` modified but uncommitted; `krux` submodule pointer updates must be committed in the submodule first, then in the outer repo (use absolute paths).
- ADB intermittently drops the phone after reboots; `adb kill-server && adb start-server` recovers it.
- `/tmp/opencode` is wiped by reboots — keep tooling in `tools/`.

## 2026-11-09 17:05:55 UTC - session compaction (auto)

* session: `ses_e7aba84c4ffewu0RUZojGclCWS`

## Objective
- Make the Nexus 5 camera reliable for QR scanning (computer screens, small phones) and entropy collection, with a fast preview and a high-resolution QR path.

## Requirements
- Never flash; `fastboot boot` only; cold boot when the camera pipeline needs a clean session.
- Project tree is the source of truth; device code must never be newer; deploy to `/data/krux`.
- Temporary camera state in `/tmp` (e.g. `af`, `mode`, `stream`, `bench`, `hwscale`); no symlinks in the Krux tree.
- Do not break the entropy capture while tuning QR.
- Never aim/start an interactive camera scan without asking (10 s cue, ~20 s scan, then tell the user to return).
- No modem/Wi-Fi/BT/vendor HAL; keep the delivered preview modest.
- Tablet is offline (battery depleted); the second Nexus 5 is the target display. Lights are off during AF/AE testing.

## Decisions
- **Full-res plan (user's, accepted):** switch to full-res 3280×2464, centre-crop 2160×2160, drop chroma, integer ÷2 to 1080×1080 for preview/entropy, 1:1 sub-crop for quirc.
- Assistant corrections to the plan: quirc sub-crop must be **1:1** (prefer **1280×1280**, decoder max), rotate the preview, move the publish gate off the chroma, bounce-copy only the crop, `sensor.snapshot` still throttles to 15 fps unless reduced to 33 ms.
- Shared memory: camd publishes luma to `/tmp/krux/frame` (seqlock); `A` keepalive required or camd goes idle.
- Uncached ION handled by a cached bounce buffer; build uses `-O2 -ftree-vectorize -mfpu=neon`.
- AE: `QR_MAX_EXPOSURE 0x04E0`, `DEFAULT_EXPOSURE 0x0400`, lock on decode, code-box ROI, unlock after 15 unread frames.
- `hwscale` (ISP PIX scaler) off by default — captured sequence does not configure the scaler.

## Work State
### Completed
- Binning crop bug fixed: `settings_7` lacked binning; added `0x0383/0x0387 = 0x03` (odd_inc 3) in `cam/camd.c` and `cam/imaging8.c`; full FOV restored.
- Full-res mode works (3280×2464); binned is 1640×1232. Vendor library has only these two output sizes.
- Real decode blocker found: **focus** — AF sweep settles at focus step 12 (sharpness ~1.77M); earlier frames at bad focus.
- AE fixes implemented and verified: exposure ceiling, lock-on-decode, code-box ROI, unlock-on-lost-code; `qr_capture.py` wired.
- Black background: 8 cm V20 decodes at 0x0400/0x04e0; 4 cm V2 decodes; 4 cm V20 fails (needs full-res).
- Shared memory + `A` keepalive implemented; `image.from_shared()` / `shared_status()`.
- Downscale: row-contiguous two-pass + cached bounce buffer + NEON; **230 → 45 ms**, preview **4.3 → ~15 fps**.
- CPP `EAGAIN` was buffer starvation from slow downscale; resolved by the fix.
- `tools/show-qr` now supports `--device`, `--screen`, `--background`, `--scene computer`, `--brightness`; no synthetic touches.
- `tools/scan-on-device.py` drives the app's AE/AF/decode path; timing tools added.
- Docs updated (`docs/cam-corpus-findings.md`, `docs/cam-ae-af-plan.md`).

### Active
- User's latest request: **cheap method to detect QR size in the full frame and dynamically crop to 1280×1280 (not necessarily centred, ~10 % margin for hand movement)**, avoiding pre-cropping full-res and giving proper pixel density; user assumes they will move the QR closer if detection fails.
- Design/implementation of the full-res 2160×2160 pipeline with the four corrections above.

### Blocked
- Tablet offline (battery depleted) — second Nexus 5 (`0643efdcf0eca63a`, 1080×1920, Photos Go) is the target.
- ISP PIX hardware scaler: registers not mapped from captured vendor config; `hwscale` yields no frames.
- Lights off — AF/AE-dependent tests limited.

## Next Move
1. Answer/design the dynamic QR detection + dynamic 1280×1280 crop (finder-pattern detection on the 1080 preview or the 2160 crop, then a 1:1 crop with 10 % margin).
2. Implement the full-res plan: centre 2160×2160 crop, drop chroma, integer ÷2 preview/entropy, 1:1 quirc sub-crop; fix the publish gate to use luma.
3. Measure 30 fps target (crop memcpy ~19 ms + 2×2 ~3 ms + rotate ~2 ms + sub-crop ~2 ms ≈ 26 ms vs 33 ms sensor period).
4. Re-test 4.5 cm V20 at 21 cm (full-res ≈5.4 px/module).

## Relevant Files
- `cam/camd.c`: sensor modes, downscale/bounce, shm publish, publish gate, ISP-IF crop, knobs.
- `krux/src/krux/camera.py`: AE controller, `QR_MAX_EXPOSURE`, lock/unlock, code box, `_stats`.
- `krux/src/krux/pages/qr_capture.py`: scan loop, AE follow, lock-on-decode, deferred AF sweep.
- `krux/src/sensor.py`: `snapshot()` shared-memory path, `A` keepalive, 66 ms throttle.
- `krux/src/image.py`: wrapper `from_shared`, `shared_status`.
- `krux/firmware/micropython/ports/unix/image_maixpy.c`: `from_shared`, `find_qrcodes`, stats (a/b_stdev fixed 10).
- `krux/firmware/micropython/ports/unix/k_quirc.h` / `k_quirc_internal.h`: `K_QUIRC_MAX_BITMAP 1954` (V27), `K_QUIRC_MAX_IMAGE_DIM 1280`, `K_QUIRC_MAX_PROCESS_VERSION 40`.
- `tools/show-qr`: target display (device/screen/scene/brightness).
- `tools/scan-on-device.py`: app-path scan harness.
- `tools/capture-one.py`: fixed-exposure capture via control socket.
- `scripts/cam/build-camd.sh`: camd build flags (NEON/-O2).
- `docs/cam-corpus-findings.md`: consolidated findings incl. performance section.
- `docs/cam-ae-af-plan.md`: ordered plan.
- `/tmp/boot-dev.img`: dev stub boot image (`fastboot boot`).

## Important Context
- Scanner `0879524b0068abca` (stub); target Nexus 5 `0643efdcf0eca63a` (Android, 1080×1920); tablet `41036f02470031ff` offline.
- `scan.meta` reports `w=1640 h=1232` (binned) or `3280 2464` (full); focus step 12 is the good focus.
- Downscale bench: byte-by-byte uncached ION 20 MB/s; memcpy 247 MB/s. Preview 720×960 (`OUT_W/OUT_H`); `sensor.py WIDTH/HEIGHT = 720/960`.
- Entropy uses luma-derived RGB565; chroma unused (a/b_stdev fixed at 10).
- Decoder needs ~4–5 px/module; version ceiling V27 (`K_QUIRC_MAX_BITMAP 1954`; 3917 for V28–40).
- Max distance (4 px/module): `d = 1605/N cm` binned source, `939/N cm` for 720×960 preview; 4.5 cm V20 (113 modules) ≈ 14 cm / 8 cm.
- Frame fill for 4 px/module at 1200 px: V20 ~35–38 %, V27 ~42–44 %, V40 ~59–62 %.
- ISP has multiple streams (PIX + RDI); camd currently uses one (`CAMIF_RAW`). ISP-IF crop is horizontal-only; VFE crop+scale (digital zoom) not configured.
- Commits: outer `63c4333`, `3a46648`, `c88bb3b`, `31cea80`, `950f88a`, `96c51fc`, `b16ab3c`, `2c3d82c`, `19e941f`; `krux` `7870e344`, `2592b329`, `19def53d`; micropython `332201316`. Nothing pushed.
- Phone intermittently falls back to Android; recover with `tools/boot-stub /tmp/boot-dev.img` or `adb reboot bootloader` + `fastboot -s 0879524b0068abca boot /tmp/boot-dev.img`.
- Deploy with `ANDROID_SERIAL=0879524b0068abca bash scripts/deploy-firmware.sh` (two devices connected).

## 2026-11-10 03:42:55 UTC - session compaction (auto)

* session: `ses_e7aba84c4ffewu0RUZojGclCWS`

## Objective
- Make the Nexus 5 camera reliable for QR scanning (screens, small phones) and entropy collection, with a fast preview, a high-resolution QR path, and no kernel hangs.

## Requirements
- Never flash; `fastboot boot` only; cold boot when the camera pipeline needs a clean session.
- Project tree is the source of truth; device code must never be newer; deploy to `/data/krux`.
- Temporary camera state in `/tmp` (e.g. `af`, `mode`, `stream`, `bench`, `hwscale`); no symlinks in the Krux tree.
- Do not break the entropy capture while tuning QR.
- Never aim/start an interactive camera scan without asking (10 s cue, ~20 s scan, then tell the user to return).
- No modem/Wi-Fi/BT/vendor HAL; keep the delivered preview modest.
- Full-res plan: 3280×2464 → centre 2160×2160, drop chroma, integer ÷2 → 1080×1080 preview/entropy, 1:1 sub-crop for quirc (prefer 1280).
- Cap processing at QR V20; the real sample is V17.
- Dynamic QR detection and crop to 1280 (not necessarily centred), ~10% margin.
- Fix the preview flicker/bands; entropy needs AE/AF controls and an on-screen instructions line.
- Build everything with the host's `arm-linux-gnueabihf` (hard-float); Debian armel, Linaro/AOSP and ARM GNU toolchains are obsolete.

## Decisions
- V20 cap is enough (Sparrow sample is V17, 85 modules, 923-byte UR); decodes on host and device.
- Dynamic crop: locate on the 1080 preview, request a 1:1 crop from the 2160 bounce; new `/tmp/krux/crop` shm + `C x y w h` command.
- Gate keys off the chroma (sampled from `src_uv`) to drop the ISP's dark raw frames; luma-only gate caused the dark overlay.
- Full-res pipeline implemented (2160 crop → 2×2 → 1080 preview), but **binned is the default** for stability; full-res is unreliable after warm boots.
- CPP retry is 3×2 ms: 250 ms starved the ISP (tasklet overflow → `wdog_bark`); zero retry deadlocked the CPP.
- **Do not `_exit`/restart camd**: this kernel's teardown is broken (`Invalid close`, `csid invalid state`) and the re-init overflows the ISP-IF PIX0/tasklet → `wdog_bark`.
- Deploy with `DEPLOY_RESTART=0` + cold boot; `ctl.stop/start krux` can wedge the display (`pan_idle`).
- Kernel NULL check in `msm_cpp.c` (`GET_EVENTPAYLOAD` returns `-ENODATA` on empty) is the correct drain fix; kernel rebuilt with the host toolchain.

## Work State
### Completed
- Binning crop fix; full-res mode works.
- V20 cap (`K_QUIRC_MAX_PROCESS_VERSION 20`, grid 97, bitmap 1177) + `k_quirc_scan_*` returning located-but-undecoded candidates; persistent decoder.
- Dynamic QR crop: `C` command, `/tmp/krux/crop`, `image.from_crop`, `qr_scan`, AE box from located candidate; validated on device (crop 399×399; sample decodes).
- Full-res pipeline: 2160 centre crop, 2×2 → 1080 preview, luma gate; daemon ~30–35 fps; stale-frame fix in `sensor.snapshot`.
- Entropy: mode-aware VTS (skip in full-res), seed at max exposure/gain, ignore AF sweep, AE follow, portrait preview, meter every frame after image; dark 4.4–4.8 bits/px, bright 4.8–5.0.
- Toolchain record corrected: host `arm-linux-gnueabihf` default; armel/Linaro/AOSP/ARM GNU marked obsolete; explicit `-mfloat-abi=hard` in micropython, camd, imaging, stub init.
- Kernel CPP drain fix built and verified (`queue eventdata new max is 1`, was 7000+); camera 7.88–8.97 fps, 0 failed.
- 250 ms EAGAIN block removed (fixed the tasklet overflow → `wdog_bark`).
- Chroma gate restored (samples `src_uv`, `us < 8` dropped); `[pub] s=32 us=46 valid=1`.
- Short CPP retry (3×2 ms) fixed the zero-retry deadlock.
- Preview: single-pass `lcd.display(bg=...)` writes image + background together (replaced two-pass clear+image); fast row-major blit.

### Active
- Camd `_exit` watchdog removed (built `e84be659`, **not yet deployed/committed**); camd should stay up and a stall leaves the camera dead without crashing the box.
- Preview still flickers; bands/whole-image blink (~0.7 s) unresolved.

### Blocked
- Preview flicker/0.7 s blink: likely display commit/`pan_idle` vsync path; needs framebuffer capture during the preview to separate draw vs panel.
- Kernel/ISP stall: `VFE0 pix0 overflow` + `Tasklet queue overflow` after ~14 min idle; teardown/re-init is broken on this kernel; root cause (ISP clock/VFE) not isolated.
- Full-res session unreliable after warm boots (`bytesused=0`, dropped frames); needs true power-cycle.

## Next Move
1. Commit the camd change removing the `_exit` watchdog (`e84be659`).
2. Deploy it with `DEPLOY_RESTART=0` and cold-boot (`fastboot boot /tmp/boot-dev.img`).
3. Capture `/dev/graphics/fb0` repeatedly during the QR preview to determine whether the 0.7 s blink is in the framebuffer or only the panel commit.
4. Investigate the `VFE0 pix0 overflow`/tasklet overflow (idle streaming, ISP/VFE clock/bandwidth) as the remaining kernel-hang root cause.
5. Re-test full-res after a true power-cycle; confirm the chroma gate and preview on the new camd.

## Relevant Files
- `cam/camd.c`: sensor modes, 2160 crop, bounce, downscale, gate, CPP retry/drain, crop shm, watchdog.
- `krux/src/sensor.py`: snapshot, shm new-frame check, reconnect, `crop()`.
- `krux/src/image.py`: `from_shared`, `from_crop`, `qr_scan`, `shared_geometry/reset`.
- `krux/src/krux/pages/qr_capture.py`: scan loop, `_retry_crop`, AE box.
- `krux/src/krux/pages/capture_entropy.py`: entropy controls, meter, AE follow, portrait.
- `krux/src/krux/camera.py`: AE/VTS/entropy seed, `adjust_exposure`, `refocus`.
- `krux/src/krux/display.py`: `render_image` one-pass `bg`.
- `krux/firmware/micropython/ports/unix/lcd_bcm.c`: commit, double-buffer (reverted), fast/single-pass blit.
- `krux/firmware/micropython/ports/unix/image_maixpy.c`: `from_shared/from_crop/qr_scan`.
- `krux/firmware/micropython/ports/unix/k_quirc.c` / `k_quirc_internal.h` / `k_quirc_identify.c`: V20 cap, scan API.
- `linux/drivers/media/platform/msm/camera_v2/pproc/cpp/msm_cpp.c`: `-ENODATA` NULL check.
- `tools/build-kernel.sh`, `rootfs/make-boot-image.sh`, `rootfs/build-stub-init.sh`: kernel/boot/toolchain.
- `docs/build-guide.md`, `docs/toolchain.md`: corrected toolchain record.
- `/tmp/boot-dev.img`: dev stub boot image (new kernel + ramdisk).
- `scripts/deploy-firmware.sh`: deploy (`DEPLOY_RESTART=0` to avoid the display hang).

## Important Context
- Scanner `0879524b0068abca`; target Nexus 5 `0643efdcf0eca63a` (1080×1920, Photos Go); tablet offline.
- Kernel `3.4.113-...-g0cd43625d3c`; firmware `d9048c20`/`1a684f2`; deployed camd `01ef582d`; built camd `e84be659` (not deployed).
- Preview resolution: **720×960** binned (default), centred on 1080×1920 (180 px side, 480 px top/bottom margins); full-res is 1080×1080.
- `/proc/cmdline` shows `bootreason=wdog_bark`; read the stub's log with `adb shell 'su -c "cat /proc/last_kmsg"'` (Android root available via `su`).
- Crash chain: `VFE0 pix0 overflow` → `msm_isp_close_node: Invalid close` → `csid invalid state 1` → `Tasklet queue overflow` → `Watchdog bark`; caused by camd restart/teardown.
- Kernel build: `bash tools/build-kernel.sh --tree linux --out /tmp/opencode/ko`; boot image: `bash rootfs/make-boot-image.sh artifacts/twrp-3.7.0_9-HH.R.17-MADV_WIPEONFORK.img /tmp/opencode/ko/arch/arm/boot/zImage-dtb /tmp/boot-dev.img`.
- `sensor.snapshot()` throttle 66 ms (15 fps); daemon produces ~30–35 fps.
- Entropy "Good when covered" is expected: the camera noise is the entropy source; the `a/b_stdev=10` floors the variance index.
- Commits (outer): `a48290d`, `ef64e52`, `819eef7`, `1a684f2`, `6da3ed8`, `df8ee67`, `1aa73ca`, `884c04d`; `linux` `0cd43625d3c`; uncommitted camd edit `e84be659`.
- The unrelated `docs/compaction-log.md` is modified and intentionally not committed.

## 2026-11-10 09:43:55 UTC - session compaction (auto)

* session: `ses_e7aba84c4ffewu0RUZojGclCWS`

## Objective
- Make the Nexus 5 camera reliable for QR scanning (no kernel reboots, no stalls) and eliminate the "pesky stripes"/banding artifacts in the preview; the user is away and wants the work done without stopping.
- The user asked for: (1) kernel stall fix, (2) ISP-config bisection for the stripes, (3) measure whether stripes improved.

## Requirements
- Never flash; `fastboot boot` only; cold boot when the camera pipeline needs a clean session.
- Project tree is the source of truth; device code must never be newer; deploy to `/data/krux`.
- Temporary camera state in `/tmp` (af, mode, stream, bench, hwscale); no symlinks in the Krux tree.
- Do not break the entropy capture while tuning QR.
- Never aim/start an interactive camera scan without asking (10 s cue, ~20 s scan, then tell the user to return).
- No modem/Wi-Fi/BT/vendor HAL; keep the delivered preview modest.
- Full-res plan: 3280×2464 → centre 2160×2160, drop chroma, integer ÷2 → 1080×1080 preview/entropy, 1:1 sub-crop for quirc.
- Cap processing at QR V20; the real sample is V17 (923-byte UR).
- Dynamic QR detection and crop to 1280 (not necessarily centred), ~10% margin.
- Fix preview flicker/bands; entropy needs AE/AF controls and an on-screen instructions line.
- Build everything with the host's `arm-linux-gnueabihf` (hard-float).
- User's three requested tests: (1) full-res frames cropped linearly to fit the screen with all other processing disabled; (2) same in binned mode; (3) compare raw frames to what the framebuffer displays, to see whether artifacts appear in both.
- User wants the tasks done without stopping; user is away for a while.
- The user believes the stripes are implementation-triggered and suggested walking back the register writes to the driver/sensor until the image clears or frames stop.
- New device: the second Nexus 5 was replaced by an LG LGL322DL (Android 10, 720×1440, adb, no root) — serial `LGL322DL71cd2c3`; the Sparrow QR must be shown on it.

## Decisions
- Do NOT `_exit`/restart camd: on this kernel the teardown is broken (`Invalid close`, `csid invalid state`, `NULL stream`) and the re-init overflows ISP-IF PIX0/tasklet → `wdog_bark`. Reverted the STREAMOFF/STREAMON restart after it caused `No free write masters` → `Request validation failed` → watchdog.
- Stall watchdog must not depend on `latest_seq`; it now counts consecutive 2 s poll timeouts.
- Runtime exposure/gain writes must use the sensor's **group hold** (0x0104=1 → registers → 0x0104=0), matching vendor `imx179_settings_3/_7/_6`; bare writes apply mid-readout and produce moving bands.
- CPP frame-done thread must sleep 1 ms per event (no sleep on success pinned a core at ~100% CPU).
- Poll/DQBUF error paths must back off 100 ms instead of busy-spinning; same-buffer repeats detected as a stall.
- The daemon demosaics the raw Bayer itself before downscaling (green samples as luma; green interpolated at R/B sites) instead of relying on the CPP.
- `ispcfg` bisection knob added at `/data/krux/ispcfg` (how many of the 56 initial ISP uploads to send); `mode`, `stream`, `capture_cfg` now read from persistent `/data/krux/state` via `state_read_persist()` (because `/tmp` is tmpfs and is wiped on reboot).
- Vendor TWRP init still runs as PID 1 alongside our `krux-init`; it is minimal (ueventd, logd, adbd) and does not touch the camera — the screen dimming is the Krux firmware's own screensaver.
- The Sparrow QR was pushed to the LG phone (`/sdcard/Download/sparrow.png`) and opened full-screen via Gallery.

## Work State
### Completed
- Kernel crash root-caused: camd's stall watchdog `_exit(1)` → supervisor restart → broken teardown → tasklet overflow → `wdog_bark`. Fixed by never exiting (commit `9c2a91a`).
- CPU spin fixed: `cpp_event_thread` 1 ms backoff + poll/DQBUF backoff + same-buffer stall detection (commit `2320fcf`).
- Group-hold exposure/gain writes added (`sensor_set_exposure_gain`).
- Bayer demosaic before the binned downscale (commit `57c0462`); QR now decodes 923 bytes via the 1:1 crop retry (`crop decode 923 bytes`).
- ISP-config bisection run at counts 56/50/45/40/35/30/25/20/15/10/5 — the band is **identical at every count** (12.0→13.2→16.0→9.0→0.0 by 64-row bins), so it is **not** in any ISP upload.
- Disabling `cam_capture` also left the band unchanged (so `0x444`/`0x464` geometry overrides are not the cause).
- 6 source dumps are byte-identical → the band is stable, not a race.
- With the lens covered the source is **not uniform black** — it has a hard row staircase with the last 80 rows exactly 0.0 (never written) → stale/misaligned buffer content, i.e. the ISP delivers fewer lines than requested.
- Kernel exclude-input experiment (CPP output ≠ input) made it worse (GET_BUF found no free buffer, 0.5 fps) → reverted; kernel tree clean at `0cd43625d3c`.
- Persistent knobs (`/data/krux/state`) implemented and built (`fe52d97b`).

### Active
- Finding why the ISP delivers fewer lines than the 1640×1232 requested (stale rows at the bottom of the source). Next lead: the ISP-IF crop is **horizontal only** (`crop_start_pixel`/`crop_end_pixel`, `msm_ispif.c:442`) — check for a vertical crop / line-count path that was never configured.
- Verifying the persistent-knob bisection (mode/stream/capture_cfg) across reboots now that `state_read_persist` exists.

### Blocked
- The camera stalls after ~10–15 min (`CFG: EAGAIN`, one buffer cycling, `no frame after 2s (121)`); only a cold boot recovers it. Root cause traced to the CPP processing **in place** (`out=5 in=5`) collapsing the ISP raw and CPP processed passes onto one buffer; a correct fix needs the ISP shared-buffer/divert accounting reworked.
- Second Nexus 5 replaced by the LG phone; the Sparrow QR is now displayed there but the camera must be re-aimed and re-tested against it.

## Next Move
1. Read `linux/drivers/media/platform/msm/camera_v2/ispif/msm_ispif.c` around the crop handling and the ISP-IF CFG/frame-boundary path to find the vertical line count; check `cam/camd.c` `ispif_config()` (only `crop_start_pixel`/`crop_end_pixel` are set) for a missing line/crop field.
2. Deploy the persistent-knob build (`fe52d97b`) and run the user's matrix, capturing source + framebuffer pairs at the same instant: mode=0/1 × stream=0/1 (CAMIF_RAW vs PIX_VIEWFINDER), plus `rawcrop` (ctl `c`) for the linear 1:1 no-processing view.
3. Implement the stall fix in the kernel: make the CPP output buffer differ from its input **and** keep the buffer accounting consistent (the earlier exclude attempt failed because no other buffer was free) — e.g. reserve a dedicated output buffer set or fix the ISP shared-buffer divert/put ordering.
4. After the fixes, re-measure stripes/banding and report.

## Relevant Files
- `cam/camd.c`: daemon — sensor/ISP/CPP setup, `ispif_config()` (horizontal-only crop), `cpp_send_chromatrix()`, `sensor_set_exposure_gain()` (group hold), `state_read_persist()`, `isp_cfg_count` bisection knob, `rawcrop` diagnostic (ctl `c`), demosaic + downscale, stall/backoff logic.
- `cam/cam_chromatrix.h`: 266-word vendor CPP message; `num_stripes` derived from `msg[12]` = 5.
- `cam/cam_config.h`: 56 initial ISP uploads (`cam_initial`) — the bisection target.
- `cam/cam_capture.h`: vendor capture ISP config (`0x444=0x403f`).
- `cam/cam_cycle.h` / `cam/cam_cycle_preview.h`: per-frame ISP cycles (capture vs preview geometry).
- `cam/sensor_live.h`, `cam/imx179_regs.h`: sensor mode/bin (0x0383/0x0387 = 3) and group-hold sequences.
- `linux/drivers/media/platform/msm/camera_v2/pproc/cpp/msm_cpp.c`: CPP `msm_cpp_cfg()` (GET_BUF/`-EAGAIN`), `msm_cpp_notify_frame_done()` (BUF_DONE, `processed_divert`).
- `linux/drivers/media/platform/msm/camera_v2/msm_vb2/msm_vb2.c`: `msm_vb2_get_buf()` / `in_freeq` accounting.
- `linux/drivers/media/platform/msm/camera_v2/isp/msm_buf_mgr.c`: `msm_isp_buf_done()`, `buf_put_count`, `ISP_SHARE_BUF_CLIENT = 2`.
- `linux/drivers/media/platform/msm/camera_v2/ispif/msm_ispif.c`: ISP-IF crop (`crop_start_pixel`/`crop_end_pixel`, lines 442-443).
- `rootfs/init-stub.c`: stub init; `service krux /sbin/krux-init`; camd exec args (`... "binned" "cfg" "nocycle" ...`).
- `krux/src/krux/pages/qr_capture.py`, `krux/src/krux/pages/capture_entropy.py`: scan/entropy loops.
- `scripts/deploy-firmware.sh`: deploy (`DEPLOY_RESTART=0` to avoid the display hang).
- `tools/build-kernel.sh`, `rootfs/make-boot-image.sh`: kernel + boot image (`/tmp/boot-dev.img`).
- `/tmp/opencode/bisect.sh`, `/tmp/opencode/bandtest.sh`, `/tmp/opencode/ctldump.py`: bisection/dump harnesses (device-side `ctldump.py` also at `/tmp/ctldump.py` on the device).

## Important Context
- Device serials: camera Nexus 5 = `0879524b0068abca` (stub/Krux, root adb); QR-display phone = `LGL322DL71cd2c3` (LG, no root).
- Current device state: stub booted with kernel `3.4.113-…-g0cd43625d3c`, camd `b928408` (event-log build, ~4.4 fps, no tasklet/bark). The `fe52d97b` build (persistent knobs) is built but NOT yet deployed.
- Commits: `9c2a91a` (no exit on stall), `082c8d2` (CPP input from returned-buffer ring), `2320fcf` (CPU spin + group hold + ispcfg knob + rawcrop), `57c0462` (Bayer demosaic), `968e01a` (document no restart), `6b03187` (CPP frame-done log). `docs/compaction-log.md` is modified and intentionally uncommitted.
- `fastboot boot` is flaky — it sometimes lands in Android; retry until `adb devices` shows `recovery`.
- The band profile in the raw source (rows of 1232, 64-row bins): `12.0` for rows 0–704, `13.2` at 704–768, `16.0` for 768–1088, `9.0` at 1088–1152, `0.0` for 1152–1232.
- The CPP frame-done log shows `out=5 in=5 divert=0` — the CPP processes **in place**.
- `ispcfg` bisection already disproved: the ISP initial uploads and `cam_capture` are not the cause of the band.
- With the lens covered the raw source is Bayer-flat (checkerboard 0.00) but still shows the staircase — i.e. the band is not a Bayer artifact; rows below ~1152 were never written by the ISP.
- Environment note: `adb shell` on the LG phone works without root; `pkill -f <pattern>` can match the invoking shell's own command line (use a bracketed pattern).

## 2026-11-10 03:40:00 UTC - session compaction (manual)

## Objective
- The three tasks the user asked for, done without stopping while they were away:
  (1) the camera **stall** fixed in the kernel, (2) the **ISP-config bisection**,
  (3) a **measurement** of whether the stripes/bands improved.
- The camera had to stay reliable for the QR scan (the Sparrow QR is displayed on
  the LG phone, which the camera is aimed at).

## Root cause of the stall (and of the "bands") — found this session
- The ISP takes a vb2 buffer for every frame it writes.  **Nothing ever gave it
  back**: the ISP's shared-buffer accounting (`ISP_SHARE_BUF`, `buf_put_count`)
  wants two puts per buffer, and the second one never comes — the CPP has a
  buffer manager of its own (`msm_generic_buf_mgr`) and never touches the ISP's
  buffers, and the HAL's `ENQUEUE_BUF` ioctl is never issued on this stack.
- So every frame leaked one buffer.  The pool (8 buffers) drained after a few
  frames, the ISP stopped getting buffers, the picture froze on a stale frame,
  the CPP could not get an output buffer either (`CFG: EAGAIN`), and the stream
  stalled.  Only a cold boot recovered it.
- The "bands" were a symptom of the same leak: the source the daemon read was a
  **stale frame** (the last frame before the pool ran dry), so it was a flat,
  hard-edged staircase with the bottom 80 rows never written.  It was identical
  in every dump because it was the same frozen frame — not an ISP artifact.
- Proven with new kernel instrumentation (per-buffer get/put/done counters and a
  pool dump when the pool runs dry): every buffer had `in_freeq=1` and `put=0`,
  i.e. checked out and never returned.

## The fix (kernel, `04893de2b9b`)
- `msm_isp_axi_util.c`: the buffer the ISP has just written is **held until the
  next frame of the same stream is done** (by then the CPP has read it) and the
  one held before it is released then.  The buffer is given back on both paths
  (diverted to the CPP or not).
- Result: no `no free buffer`, no dropped frames, no stall; 187 fps of frame
  deliveries (the daemon is not paced to the sensor) instead of 5.

## The ISP-config bisection — repeated on the *fixed* pipeline
- The first bisection (56/50/45/40/35/30/25/20/15/10/5 uploads) was meaningless:
  it measured a frozen frame, so every count looked the same.
- Repeated after the fix: counts 56, 45, 35, 25, 5 → the frames are **byte
  identical**.  The ISP's initial uploads are still **not** the cause.

## The measurement (the user's three tests)
- (1) full resolution, linear 1:1 crop, no other processing (`rawcrop`, mode=1):
  the row profile is smooth; the largest row step is 2.3 (scene edges).
- (2) the same in binned mode (mode=0): smooth; the only steps are the QR's own
  edges (16.2 at row 118, −13.8 at row 485).
- (3) raw frame vs what is displayed: the camera page renders the published
  preview (the shared region `/tmp/krux/frame`), and the preview and the source
  show the same structure, both without bands.
- Before the fix (the stale frame): the flat lower region of the source had a
  row-to-row step of up to **14.35** and a std of **6.33**.  After: **0.91** and
  **0.199** — 33× smoother.

## Also found
- The **full-resolution mode** needs the vendor's **per-frame ISP cycle**
  (`cam_cycle`, the capture geometry).  Without it the frame has a black band of
  64 rows and a step of 19.4; with it the frame is smooth.  New knob `cycle`.
- The **PIX_VIEWFINDER** stream still delivers no frames (`fps=0`): a separate
  problem, not touched here.
- The **build script** swallowed the compiler's errors and left the old binary in
  place, so a broken change looked like a successful build.  Fixed.
- `tools/send-key` wrote a 64-bit timeval into a 32-bit kernel's input events, so
  the keys never reached the firmware.  Fixed (the timeval is now two 32-bit
  words), but the firmware still did not react, so the camera page could not be
  opened for a framebuffer capture; the shared-memory preview was used instead.

## Commits
- kernel `04893de2b9b` — give the ISP's frame buffer back to the pool (+ the
  pool/buffer diagnostics).
- `51319fc` — the pipeline knobs are read from `/data/krux/state`, so they
  survive the cold boot a test needs.
- `b05dabf` — the `cycle` knob, the dump in the raw-crop path, the build-script
  fix, the send-key fix.
