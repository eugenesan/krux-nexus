# TODO — remaining tasks for project completion

## Camera

### Autofocus (docs/cam-autofocus.md)
- [x] Capture the DW9714 table (45 steps, 9 codes/step, initial code 169)
- [x] Capture the vendor HAL call sequence (`open → GET_INFO → SET_INFO →
      SET_DEFAULT_FOCUS(7) → MOVE`, hunts while streaming)
- [x] Replay the sequence in camd after sensor power-up, before the mode table
- [x] Add `/data/krux/af` (0-4) bisect and `/data/krux/focus` fixed step
- [x] Confirm level 1 (`open` + `GET_INFO`) keeps the stream alive
- [ ] Confirm the boot image actually carries the newest camd (one run used an
      older binary: `cfg 1 → cfg 3`, no `cfg 2`)
- [ ] Read the `actuator init:` parsed params and reconcile
      `msm_actuator_set_info_t` with the kernel layout
- [x] Fix the `SET_INFO` / real-lens-move stream stall: `damping_step` must be non-zero
- [x] Calibrate the focus step with a lit scene at ~40 cm (sweep found step 19)
- [ ] Replace the coarse contrast sweep with the vendor-style hunt; make
      startup AF the default

### Integration/wiring (2026-10-30 late)
- [x] Fix `_image.from_nv12` argument/lifetime and use GC-managed image buffers
- [x] `sensor.snapshot()` header/ready/sequence handling and 15 fps client throttle
- [x] Add real luma statistics plus min/max/clipping counts
- [x] Add the `shannon.entropy_img16b()` module
- [x] Add camd `W`/`E`/`V`/`Q`/`S` socket commands in the source
- [x] Start camd from the firmware init stub and bundle it in the ramdisk
- [x] Smoke test (320×240), entropy statistics, `shannon`, and QR-decode path
- [x] Rebuild the stub ramdisk/boot image and verify camd auto-start on a cold boot
- [ ] Real static QR and animated UR in the full Krux UI
- [ ] Entropy page in the full Krux UI (well-lit mid-gray scene)
- [x] Build a stable control-capable camd and enable AE (bounded mean-driven AE with clipping backoff; converges in lit scenes)
- [ ] 50 reset/run/close cycles and 30-min stability on the chosen path
- [x] Decide whether PIX is needed for 20 fps/10 fps animated UR; CAMIF_RAW+CPP at ~15 fps is the chosen path for now

### Stage 4 acceptance (docs/cam-bringup-stage4.md)
- [ ] QR decode on real QR code (use a lit LCD/printed code; fixed vendor exposure for now)
- [ ] Scene-dependent entropy (l_stdev varies with scene)
- [ ] 50 reset/run/close cycles (no crash, no camif error)
- [ ] 30-min long-run stability (no oops, reboot, fd leak, >8 KB GC loss; 20/15 fps animated, 8 fps static)
- [ ] Air-gap verification (boot air-gap kernel, check no new interface/socket/module)
- [ ] camd not running when no camera page is open

### Auto-exposure (docs/cam-bringup-auto-exposure.md)
- [x] Requirements confirmed (2026-10-30): animated UR 5–10 fps; QR on LCD/OLED screen; 20 fps capture / 15 fps decode target; exposure lock allowed; entropy mid-gray/no-clip
- [x] Step 1: camd "W"/"E" socket commands (verified: response b"OK")
- [x] Step 2: sensor.py `__write_reg` + `set_exposure`/`set_gain` (verified: no error)
- [x] Step 3: verify register writes during streaming (verified 2026-10-30: Y mean 2 -> 9 before the fix, then 0 -> 10 -> 28 after the chroma fix; 0 IOMMU faults)
- [ ] Step 4: exposure/VTS/gain sweep at fixed 20 fps and 15 fps VTS; `set_vts` is now in `sensor.py`, but the validated runtime camd lacks W/E/V, so this needs a stable control-capable build
- [ ] Step 5: AE algorithm in Python: `qr_screen` (p90/p10, anti-banding ladder, fixed VTS, lock) and `entropy` (mid-gray/no-clip); `Camera._auto_expose()` is implemented but disabled
- [ ] Step 6: move AE to camd (C), with mode/lock/manual commands
- [ ] Step 7: integrate with Krux camera pages (QR waits for convergence; entropy mode; manual override)

### VFE IOMMU fault / stability (docs/cam-stability-thermal.md)
- [x] Thermal vs software investigation (conclusion: software; `wdog_bark`, not thermal)
- [x] Instrumented harness (thermal sampler + detached supervisor + last_kmsg collection)
- [x] Confirm AE writes are not the trigger (faults start before the test connects)
- [x] Profile the camd per-frame path: debug path was 400 ms + 18 MB/frame; 5x5 scalar downscale 187-250 ms; 3x3 wide-load downscale 34-44 ms; total 36-45 ms, **22-28 fps** (docs/cam-frame-path.md)
- [x] Disable `MSM_CPP_DUMP_FRM_CMD` (186 printk lines per timed-out frame)
- [x] Fix `msm_cpp_do_timeout_work`: drop the frame and return; do not `msm_cpp_write()`/resend from the workqueue
- [x] Guard `msm_isp_process_stats_irq` on the CAMIF_RAW path
- [x] Cherry-pick the CPP timeout fix onto `airgap/master-cleanup` (`cdcc09b3682`), rebuild the kernel/boot image, and run a short smoke test (5 frames, no CPP panic)
- [x] Add the `y_only` VFE completion flag and re-apply the Y-only composite/IRQ mask after VFE uploads
- [x] Mask the CAMIF `0x660` error IRQ for the Y-only path; 30 s test: 0 ping-pong, 0 tasklet, 2468 frames, no watchdog, 3-frame smoke passed
- [x] Re-run 10-minute / 1,000-snapshot stability on the y-only path: 50,798 frames/10 min, max 63 °C, no watchdog; 1,000 snapshots GC loss 2,304 bytes, stable
- [ ] Real QR/UR and entropy pages on the y-only path
- [ ] Re-enable/implement the QR and entropy AE policy
- [x] Re-run the paced 20 fps profile with the Python `snapshot + find_qrcodes` loop: no panic/timeout/dump; 20.0 fps in steady groups, ~17.3 fps average, 59 ping-pong warnings
- [x] Re-run the paced 20 fps profile with the Python `snapshot + find_qrcodes` loop: no panic/timeout/dump; 20.0 fps in steady groups, ~17.3 fps average, 59 ping-pong warnings
- [x] Full-height CBCR + full-height chroma buffer: ping-pong 0, but CPP stalls at ~24 frames with `CFG: EAGAIN`; not viable with the captured CPP message
- [x] Measure sustainable CAMIF_RAW+CPP rate: 20 fps stalls at ~300 frames; 15 fps survives 30 s at ~13.7 fps average; 165 ping-pong warnings
- [ ] Decide the 20 fps path: PIX_VIEWFINDER/VFE scaler (requires the VFE overflow fix) or accept the ~15 fps floor
- [ ] Add a blocking `snapshot` waiting for a new `latest_seq` (still useful for animated QR, after the path decision)
- [ ] Re-tune the downscale (NEON/cached/affinity) only if the chosen path still uses the CPU downscale
- [ ] Re-enable the 30-min Stage 4 stability run on the chosen path
- [ ] Check the remaining `camif error 0x660` input-side messages

### VFE overflow fix (docs/cam-bringup-vfe.md) — optional, after the fallback test
- [x] Source analysis (done)
- [x] Instrumentation patches (VFE40 clock/hw/WM, ISP-IF input)
- [x] Build + boot instrumented kernel (on top of the CPP-timeout fix)
- [x] Run PIX_VIEWFINDER + capture dmesg: 0 frames, 301-599 `pix0 overflow`, 1 `camif violation`
- [x] Compare against CAMIF_RAW on the same kernel: 93 frames, 0 overflow, 0 violation
- [x] Rule out clock (320 MHz), hw version (8974 V2), input selection, CAMIF last_pixel, cam_cycle/capture, VFE PIX0 enable
- [x] Capture the vendor preview ISP sequence: full-res 3280×2464, preview initial/cycle (`cam_config.h`, `cam/cam_cycle_preview.h`); the vendor has no binned preview sensor mode
- [x] Test full-res + preview initial/cycle and binned + preview cycle; 260 MHz removes input overflow but the VFE write master still does not complete
- [ ] Capture the vendor CSID/PHY/ISP-IF/VFE CAMIF/WM output state during live preview
- [ ] Apply a PIX fix and verify 0 overflow + frames
- [x] Use CAMIF_RAW+CPP as the initial fallback (fast unpaced build + 15 fps Python client)

### Optional
- [ ] Auto-exposure via I2C registers (0x0202/0x0203 exposure, 0x0204/0x0205 gain)
- [ ] Auto-exposure in camd (automatic, no Python involvement)
- [ ] Manual brightness slider in Krux settings

## Memory wipe

### Kernel wipe mode (docs/memory-wipe-state.md)
- [x] Add `wipe=` early parameter and `CONFIG_MEMORY_WIPE`
- [x] Wipe lowmem from the bootmem bitmap (not `memblock.reserved`)
- [x] Wipe highmem via `kmap_atomic_pfn`
- [x] `wipe=test[:MB]`, `wipe=verify`, `wipe=info`
- [x] Defer poweroff to `kernel_init()` so the PMIC path is used
- [x] Verify `wipe=test` (1487 MB) and `wipe=1` (poweroff) on hardware
- [x] Fix STRICT_MEMORY_RWX, V2 watchdog, DTB/r2, initrd and load address
      for the kexec path
- [ ] Make the kexec handoff reach the new kernel (blocked: needs serial
      console; see docs/memory-wipe-continue.md)
- [ ] Wire wipe into both shutdown and reboot in the UI
- [ ] Cover panics/watchdog resets
- [ ] Decide on the non-kexec fallback if kexec stays unreliable

## NFC / SeedKeeper

- [ ] Final air-gap audit (no radios, no privileged/signature permissions)
- [ ] Read-only firmware image verification
- [ ] NFC stress test (100+ operations)

## Build / packaging

- [ ] Final firmware image build (bundled ramdisk + kernel)
- [ ] Verify boot image size < 16 MB
- [ ] Verify no debug symbols in production build

## Documentation

- [ ] Update docs/cam-state.md with final Stage 4 results
- [ ] Update docs/cam-plan.md with final camera decisions
- [ ] Update docs/project-log.md with completion entry
- [ ] Final review of all docs for accuracy