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
