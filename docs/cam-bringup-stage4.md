# Camera bring-up — Stage 4: testing and fixing

Paste this into a fresh session.  Start only after Stage 3 has produced a
non-empty QVGA frame over the Unix socket and the smoke test passes.

## Goal

Complete the remaining acceptance items from `docs/cam-plan.md` §D1–D5:
QR decode on real QR codes, scene-dependent entropy, 50 reset/run/close
cycles, 30-min stability, and air-gap verification.

## Read first

1. `docs/cam-bringup-stage3.md`
2. `docs/cam-plan.md` §D1–D5
3. `docs/cam-state.md` (2026-10-29 Stage 3 status)
4. `cam/camd.c`, `krux/src/sensor.py`, `krux/src/image.py`
5. `micropython/ports/unix/image_maixpy.c`, `micropython/ports/unix/lcd_bcm.c`

## Current state

- camd captures QVGA frames (centre-crop 1600×1200, 5×5 box-average to 320×240)
  and serves them over `/data/krux/camd.sock` (Unix socket, not TCP).
- sensor.py implements the MaixPy-compatible API (reset, get_id, set_pixformat,
  set_framesize, snapshot, close, etc.).
- _image native module: Image, width, height, to_bytes, from_nv12,
  get_statistics, find_qrcodes (quirc integrated).
- Smoke test passes: 10 frames 320×240, get_id() → 0x179, no SensorError.
- 100-frame stability passed (mem_free stable at ~94 KB).
- CPP streaming fixed with retry + 50ms delay (0 EAGAIN errors).
- LCD display: lcd.display(Image) works (RGB565→32-bit).
- Image is dark (Y mean≈1–2) due to fixed exposure (1248/1252 lines).

## Known issues

1. **Dark image**: IMX179 uses fixed exposure (1248/1252 lines) and gain (0x00e0).
   The vendor's camera uses auto-exposure via I2C. QR detection and entropy
   collection need brighter images. Options:
   - Add `__write_reg` support in camd for dynamic exposure/gain.
   - Or require bright lighting for testing.
2. **VFE overflow**: The processed `PIX_VIEWFINDER` path is blocked by
   `VFE0 pix0 overflow`. Only the `CAMIF_RAW` path works.
3. **Device instability**: The device crashes under sustained load. Use shorter
   test runs (≤ 20 frames) and let the device cool down.
4. **TWRP overlay**: TWRP's UI renders on top of framebuffer writes. LCD
   display code works but visual confirmation is limited.

## Tasks

### Task 1 — QR decode on real QR code

**Requires:** Printed QR code, bright lighting, 15–25 cm from camera.

```sh
adb shell 'pkill camd' 2>/dev/null; sleep 1
adb shell 'cd /data/local/tmp && (./camd 1640 1232 0x40000000 rear 0 /data/krux/cpp_firmware_v1_2_0.fw 2 binned cfg nocycle 0 0x2b 0 0 300 0 0 0 0x12 0 1 0xffffffff 0 0 0 1 0 >/tmp/camd.log 2>&1 &)'
sleep 12
adb shell "cd /data/krux && ./micropython -c '
import sensor
sensor.reset(); sensor.set_pixformat(sensor.RGB565); sensor.set_framesize(sensor.QVGA)
sensor.run(1)
for i in range(20):
    img = sensor.snapshot()
    qrs = img.find_qrcodes()
    if qrs:
        print(\"FOUND:\", [q.payload() for q in qrs])
sensor.run(0)
print(\"done\")
'"
```

**Pass:** At least one `FOUND: [...]` with correct payload.

If no QR detected, try:
- Brighter lighting (direct lamp/sunlight).
- Larger QR code (fill more of the frame).
- Check frame quality: `adb shell "cd /data/krux && ./micropython -c 'import sensor; sensor.reset(); sensor.set_pixformat(sensor.RGB565); sensor.set_framesize(sensor.QVGA); sensor.run(1); img=sensor.snapshot(); s=img.get_statistics(); print(\"l_mean:\", s.l_mean(), \"l_stdev:\", s.l_stdev()); sensor.run(0)'"`

### Task 2 — Scene-dependent entropy

Point camera at different scenes (dark, bright, textured) while running:

```sh
adb shell "cd /data/krux && ./micropython -c '
import sensor
sensor.reset(); sensor.set_pixformat(sensor.RGB565); sensor.set_framesize(sensor.QVGA)
sensor.run(1)
for i in range(5):
    img = sensor.snapshot()
    s = img.get_statistics()
    print(i, \"l_mean:\", s.l_mean(), \"l_stdev:\", s.l_stdev())
sensor.run(0)
print(\"done\")
'"
```

**Pass:** l_mean and l_stdev vary with scene.

### Task 3 — 50 reset/run/close cycles

```sh
adb shell "cd /data/krux && ./micropython -c '
import sensor
for i in range(50):
    sensor.reset(); sensor.set_pixformat(sensor.RGB565); sensor.set_framesize(sensor.QVGA)
    sensor.run(1); sensor.snapshot(); sensor.run(0); sensor.close()
    if i % 10 == 0: print(\"cycle\", i)
print(\"50 cycles OK\")
'"
```

**Pass:** `50 cycles OK`, no crash, no camif error.

### Task 4 — 30-min long-run stability

Leave QR capture page running for 30 minutes. Check:
- No oops in `dmesg`.
- No reboot.
- Frame rate ≥ 8 fps.
- Battery temperature stable.

### Task 5 — Air-gap verification

Boot air-gap kernel and verify:
```sh
# No new non-loopback interface
adb shell 'cat /proc/net/dev'
# No camd running
adb shell 'ps | grep camd | grep -v grep || echo "camd not running (good)"'
# No listening socket except ADB
adb shell 'cat /proc/net/tcp'
# No new module
adb shell 'lsmod'
```

**Pass:** Only lo/sit0/dummy0 interfaces, no camd, no TCP listener except ADB (5037).

### Task 6 — Fix dark image (optional, for better QR/entropy)

Options:
- Add `__write_reg` support in camd for dynamic exposure/gain.
- Try auto-exposure via I2C registers (0x0202/0x0203 for exposure, 0x0204/0x0205 for gain).
- Use the vendor's live sequence which includes analog settings.

## Test scripts

All in `/tmp/opencode/`:
- `test_smoke.sh` — 10 frames
- `test_qr.sh` — 20 frames, QR decode
- `test_entropy.sh` — 5 frames, stats
- `test_lcd.sh` — LCD display
- `test_stability.sh` — 1000 frames
- `test_cycles.sh` — 50 cycles
- `test_airgap.sh` — air-gap checks

## Acceptance criteria

- QR capture page: 20/20 scans of the same code, 20/20 payloads; one inverted frame.
- Entropy page: l/a/b_stdev non-zero and scene-dependent.
- 1,000 snapshots: no oops, reboot, fd leak, or >8 KB GC loss.
- Air-gap: no new interface/socket/module after camera session.
- camd not running when no camera page is open.

## Known issues

1. Dark image (Y mean≈1-2) due to fixed exposure. Needs bright lighting or auto-exposure.
2. VFE overflow blocks PIX_VIEWFINDER path; only CAMIF_RAW works.
3. Device unstable under sustained load (thermal). Use shorter runs.
4. TWRP overlays LCD writes; visual confirmation limited in recovery mode.
EOF
echo done