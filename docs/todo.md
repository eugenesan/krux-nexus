# TODO — remaining tasks for project completion

## Camera

### Stage 4 acceptance (docs/cam-bringup-stage4.md)
- [ ] QR decode on real QR code (bright lighting; image is dark, l_mean≈1-2)
- [ ] Scene-dependent entropy (l_stdev varies with scene)
- [ ] 50 reset/run/close cycles (no crash, no camif error)
- [ ] 30-min long-run stability (no oops, reboot, fd leak, >8 KB GC loss)
- [ ] Air-gap verification (boot air-gap kernel, check no new interface/socket/module)
- [ ] camd not running when no camera page is open

### Auto-exposure (docs/cam-bringup-auto-exposure.md)
- [x] Step 1: camd "W"/"E" socket commands (verified: response b"OK")
- [x] Step 2: sensor.py `__write_reg` + `set_exposure`/`set_gain` (verified: no error)
- [~] Step 3: verify register writes during streaming (protocol works, brightness change unverified due to device instability)
- [ ] Step 4: exposure sweep to calibrate ranges
- [ ] Step 5: auto-exposure algorithm in Python
- [ ] Step 6: move AE to camd (C)
- [ ] Step 7: integrate with Krux camera pages

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