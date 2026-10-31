# TODO — remaining tasks for project completion

## Camera

### Integration/wiring (2026-10-30 late)
- [x] Fix `_image.from_nv12` argument/lifetime and use GC-managed image buffers
- [x] `sensor.snapshot()` header/ready/sequence handling and 15 fps client throttle
- [x] Add real luma statistics plus min/max/clipping counts
- [x] Add the `shannon.entropy_img16b()` module
- [x] Add camd `W`/`E`/`V`/`Q`/`S` socket commands in the source
- [x] Start camd from the firmware init stub and bundle it in the ramdisk
- [x] Smoke test (320×240), entropy statistics, `shannon`, and QR-decode path
- [ ] Rebuild the stub ramdisk/boot image and verify camd auto-start on a cold boot
- [ ] Real static QR and animated UR in the full Krux UI
- [ ] Entropy page in the full Krux UI (well-lit mid-gray scene)
- [ ] Build a stable control-capable camd (or stable paced build) and enable AE
- [ ] 50 reset/run/close cycles and 30-min stability on the chosen path
- [ ] Decide whether PIX is needed for 20 fps/10 fps animated UR; otherwise keep CAMIF_RAW+CPP

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