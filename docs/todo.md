# TODO — remaining tasks for project completion

## Camera

### Stage 4 acceptance (docs/cam-bringup-stage4.md)
- [ ] QR decode on real QR code (bright lighting; image is dark, l_mean≈1-2)
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
- [ ] Step 4: exposure/VTS/gain sweep at fixed 20 fps and 15 fps VTS (needs `set_vts` deployed); record p90/p10/clipping
- [ ] Step 5: AE algorithm in Python: `qr_screen` (p90/p10, anti-banding ladder, fixed VTS, lock) and `entropy` (mid-gray/no-clip)
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
- [ ] Remove the 70 ms downscale spikes: full-height CBCR + full-height chroma buffer, NEON/cached ION, or capture-thread affinity
- [ ] Add a blocking `snapshot` waiting for a new `latest_seq` (avoid duplicate frame copies/decodes)
- [ ] Re-run 20 fps until the average is consistently >=20 fps, then proceed to AE VTS/exposure sweep
- [x] Chroma-plane experiment: full height overruns the half-height buffer (IOMMU faults); half height removes them but breaks Y/C ping-pong pairing
- [x] Workaround `cam/camd.c` with `plane_cfg[1].output_height/scan_lines = height/2`
- [x] Re-run AE test with zero IOMMU faults (0 faults, Y 0 -> 10 -> 28, 53 C)
- [ ] Fix the CBCR ping-pong mismatch: keep CBCR at full height and allocate a full-height chroma buffer (`width*height`), or fix `msm_isp_axi_util.c`
- [ ] Disable `MSM_CPP_DUMP_FRM_CMD` (186 printk lines per timed-out frame)
- [ ] Re-run the 90 s stream with 0 IOMMU faults and no ping-pong/CPP storm
- [ ] Check the remaining `camif error 0x660` input-side messages
- [ ] Then re-enable the 30-min Stage 4 stability run

### VFE overflow fix (docs/cam-bringup-vfe.md)
- [ ] Source analysis (done)
- [ ] Instrumentation patches (tools/vfe-instrument.py ready)
- [ ] Build + boot instrumented kernel (device needed)
- [ ] Run PIX_VIEWFINDER + capture dmesg (device needed)
- [ ] Compare ROM vs air-gap params (device needed)
- [ ] Apply fix (clock/ISP-IF/WM/bandwidth)
- [ ] Verify (0 overflow, GOT A FRAME)

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
EOF
echo "created"