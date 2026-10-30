# Camera bring-up — Auto-exposure (auto-brightness)

Paste this into a fresh session.  This is a feature implementation task that
requires both code changes (no device needed for steps 1-3) and device
testing (steps 4-7).

## Goal

Implement automatic brightness adjustment so the camera image is usable for
QR scanning and entropy collection regardless of lighting conditions.
Currently the image is dark (Y mean ≈ 1-2) because exposure is fixed at
1248/1252 lines and gain at 0x00e0.

## Background

### IMX179 exposure/gain registers

| Register | Function | Current value | Range |
|---|---|---|---|
| `0x0202`/`0x0203` | Coarse integration time (exposure in lines) | 0x04e0 (1248) | 100-1200 (binned mode VTS=1252) |
| `0x0204`/`0x0205` | Analog gain | 0x00e0 | 0x0020-0x01ff (~1x-8x) |
| `0x0340`/`0x0341` | Frame length (VTS) | 0x04e4 (1252) | Fixed for binned mode |

Exposure is the primary control (linear with brightness, no noise).
Gain is secondary (amplifies noise).  Prefer exposure increase over gain.

### Architecture

```
Krux (Python)                    camd (C daemon)              IMX179 sensor
     |                                |                           |
     |--- "F" (frame request) ------->|                           |
     |<-- NV12 frame (320x240) -------|                           |
     |                                |                           |
     |--- "W" (write reg) ----------->|--- sensor_write_regs() -->|
     |<-- "OK" -----------------------|                           |
     |                                |                           |
     |--- "E" (set exposure+gain) --->|--- sensor_write_regs() -->|
     |<-- "OK" -----------------------|                           |
```

### Key source files

| File | Role |
|---|---|
| `cam/camd.c` | Daemon: capture + socket server. Has `sensor_write_regs()` |
| `krux/src/sensor.py` | Python client: `__write_reg()` is a no-op stub |
| `cam/imx179_regs.h` | Sensor register tables (settings_7 for binned mode) |
| `micropython/ports/unix/image_maixpy.c` | Native `_image`: `get_statistics()` returns `l_mean` |

## Tooling

| Tool | Path | Purpose |
|---|---|---|
| AE patcher | `tools/ae-patch.py` | Applies/reverts auto-exposure patches to camd.c and sensor.py |
| camd builder | `scripts/cam/build-ae-camd.sh` | Builds camd with AE support |
| Register tester | `scripts/cam/test-registers.sh` | Tests register writes manually |
| AE tester | `scripts/cam/test-ae.sh` | Tests auto-exposure algorithm |
| Exposure sweep | `scripts/cam/sweep-exposure.sh` | Sweeps exposure values, captures brightness |

### Quick start

```sh
# 1. Apply AE patches
python3 tools/ae-patch.py --apply

# 2. Build camd
bash scripts/cam/build-ae-camd.sh

# 3. Deploy
DEPLOY_RESTART=1 bash scripts/deploy-firmware.sh

# 4. Test registers
bash scripts/cam/test-registers.sh

# 5. Test auto-exposure
bash scripts/cam/test-ae.sh
```

## Tasks

### Task 1 — camd: add "W" command (register write) [no device]

**File:** `cam/camd.c`, function `socket_thread()`

Add a new socket command `"W"` that writes a single register:

```c
if (buf[0] == 'W') {
    unsigned char reg_hi, reg_lo, val;
    if (read(cfd, &reg_hi, 1) != 1) break;
    if (read(cfd, &reg_lo, 1) != 1) break;
    if (read(cfd, &val, 1) != 1) break;
    struct imx179_reg r = { (reg_hi << 8) | reg_lo, val, 0 };
    sensor_write_regs(&r, 1);
    write(cfd, "OK", 2);
}
```

Also add an `"E"` command for exposure+gain in one transaction (4 bytes):

```c
if (buf[0] == 'E') {
    unsigned char e[4];
    if (read(cfd, e, 4) != 4) break;
    struct imx179_reg regs[4] = {
        { 0x0202, e[0], 0 },
        { 0x0203, e[1], 0 },
        { 0x0204, e[2], 0 },
        { 0x0205, e[3], 0 },
    };
    sensor_write_regs(regs, 4);
    write(cfd, "OK", 2);
}
```

**Acceptance:** camd compiles, socket protocol accepts "W" and "E" commands.

### Task 2 — sensor.py: implement `__write_reg` [no device]

**File:** `krux/src/sensor.py`

Replace the no-op stub:

```python
def __write_reg(r, v):
    _connect()
    try:
        _sock.sendall(b"W" + bytes([(r >> 8) & 0xff, r & 0xff, v & 0xff]))
        resp = _sock.recv(2)
        if resp != b"OK":
            raise SensorError("write_reg failed: %r" % resp)
    except Exception as e:
        raise SensorError(str(e))
```

Add typed helpers:

```python
def set_exposure(lines):
    """Set coarse integration time (exposure) in lines."""
    __write_reg(0x0202, (lines >> 8) & 0xff)
    __write_reg(0x0203, lines & 0xff)

def set_gain(val):
    """Set analog gain (0x0020-0x01ff)."""
    __write_reg(0x0204, (val >> 8) & 0xff)
    __write_reg(0x0205, val & 0xff)

def set_exposure_gain(exposure_lines, gain_val):
    """Set both in one transaction (faster)."""
    _connect()
    try:
        _sock.sendall(b"E" + bytes([
            (exposure_lines >> 8) & 0xff, exposure_lines & 0xff,
            (gain_val >> 8) & 0xff, gain_val & 0xff,
        ]))
        resp = _sock.recv(2)
        if resp != b"OK":
            raise SensorError("set_exposure_gain failed: %r" % resp)
    except Exception as e:
        raise SensorError(str(e))
```

**Acceptance:** `sensor.__write_reg(0x0202, 0x09)` succeeds, `sensor.set_exposure(2000)` succeeds.

### Task 3 — Verify register writes work during streaming [device needed]

**Test:** Modify exposure in `imx179_settings_7` (line 64-65) and rebuild:

```c
// In imx179_regs.h:
{ 0x0202, 0x09, 0 },  // was 0x04 → 0x09 (more exposure)
{ 0x0203, 0xc4, 0 },  // was 0xe0 → 0xc4
```

Rebuild camd, deploy, capture frame, check if image is brighter (higher Y mean).

**Pass criteria:** Y mean increases from ~1-2 to > 20.

**Alternative test (no rebuild):** Use the `"W"` command via Python:
```python
import sensor
sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.run(1)
sensor.set_exposure(2000)  # increase exposure
time.sleep(0.5)  # wait for frame
img = sensor.snapshot()
s = img.get_statistics()
print("Y mean:", s.l_mean())
```

### Task 4 — Exposure sweep to find optimal range [device needed]

Sweep exposure from 100 to 1200 lines in steps of 100, capture Y mean for each.
Also sweep gain from 0x0020 to 0x01ff in steps of 0x0020.

**Script:** `scripts/cam/sweep-exposure.sh`

**Output:** Table of (exposure, gain) → Y mean. Use this to calibrate the auto-exposure algorithm.

**Pass criteria:** Y mean increases monotonically with exposure/gain.

### Task 5 — Implement auto-exposure algorithm in Python [device needed]

**File:** `krux/src/sensor.py` or a new `krux/src/auto_exposure.py`

```python
TARGET_MEAN = 80
MIN_EXPOSURE = 100
MAX_EXPOSURE = 1200
MIN_GAIN = 0x0020
MAX_GAIN = 0x01ff
EXPOSURE_STEP = 100
GAIN_STEP = 0x0020
DEADBAND = 0.2  # ±20% of target

_exposure = 1248
_gain = 0x00e0

def auto_expose():
    global _exposure, _gain
    img = sensor.snapshot()
    s = img.get_statistics()
    mean = s.l_mean()

    if mean < TARGET_MEAN * (1 - DEADBAND):
        # Too dark
        if _exposure < MAX_EXPOSURE:
            _exposure = min(_exposure + EXPOSURE_STEP, MAX_EXPOSURE)
            sensor.set_exposure(_exposure)
        elif _gain < MAX_GAIN:
            _gain = min(_gain + GAIN_STEP, MAX_GAIN)
            sensor.set_gain(_gain)
    elif mean > TARGET_MEAN * (1 + DEADBAND):
        # Too bright
        if _gain > MIN_GAIN:
            _gain = max(_gain - GAIN_STEP, MIN_GAIN)
            sensor.set_gain(_gain)
        elif _exposure > MIN_EXPOSURE:
            _exposure = max(_exposure - EXPOSURE_STEP, MIN_EXPOSURE)
            sensor.set_exposure(_exposure)
    # else: within deadband, no change
```

**Pass criteria:** After 5 frames, Y mean converges to 64-96 (within ±20% of 80).

### Task 6 — Move auto-exposure to camd (C) [device needed]

Once the Python algorithm is tuned, move it into camd for automatic operation:

1. Add a global `ae_enabled` flag (toggle via socket command `"A"` = enable, `"a"` = disable).
2. After each frame capture in the client thread, if `ae_enabled`, compute Y mean from the captured Y plane.
3. Adjust exposure/gain registers via `sensor_write_regs()`.
4. Rate-limit to 1 adjustment per 3 frames to avoid oscillation.

**Acceptance:** camd auto-adjusts exposure without Python involvement. `sensor.snapshot()` returns brighter frames.

### Task 7 — Integrate with Krux [device needed]

Update Krux camera pages:

1. **Entropy page**: Enable auto-exposure on page open, disable on close.
2. **QR scan page**: Enable auto-exposure, wait 5 frames for convergence, then start scanning.
3. **Settings**: Add a manual brightness slider that overrides auto-exposure.

```python
# In krux camera page:
import sensor
sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.run(1)
sensor.auto_expose_enable()  # enable camd-side AE
time.sleep(1)  # converge
# ... capture loop
sensor.auto_expose_disable()
sensor.close()
```

## Acceptance criteria

- Register write ("W" command) works during streaming.
- Exposure sweep shows monotonic brightness increase.
- Auto-exposure converges to target mean (64-96) within 5 frames.
- QR code is decodable in normal indoor lighting (not just bright lamp).
- No frame drops or latency increase from register writes.
- 100-frame stability with auto-exposure enabled.

## Known issues

1. **IMX179 group hold**: Some sensors require a "group hold" (0x0104) before register writes take effect. The IMX179 may need this for exposure/gain. Test and add if needed.
2. **VTS change**: Increasing exposure beyond VTS requires increasing VTS (frame length), which slows frame rate. For auto-exposure, cap exposure at VTS-50 to avoid this.
3. **Gain noise**: High gain (>0x0100) significantly increases noise. Prefer exposure increase. Only use gain as last resort.
4. **Auto-exposure oscillation**: Use a deadband and slow adjustment rate. Consider a PID controller for smoother convergence.
5. **camd thread safety**: Register writes from the socket thread while the capture thread is running. The sensor I2C ioctl is thread-safe (kernel serializes), but the frame may be captured mid-adjustment.

## Implementation order

```
Step 1 (camd "W"/"E")  →  Step 2 (sensor.py)  →  Step 3 (test writes)
                                                          ↓
Step 4 (exposure sweep)  →  Step 5 (Python AE)  →  Step 6 (camd AE)
                                                          ↓
                                                    Step 7 (Krux)
```

Steps 1-2: no device needed (code + compile).
Steps 3-7: device needed (deploy + test).

## Notes

- The `CAMIF_RAW` path (CPP firmware) is the current capture path. Auto-exposure works regardless of capture path since it only adjusts sensor registers.
- Exposure/gain changes take effect on the next frame (IMX179 applies them at frame boundary).
- The vendor's camera HAL does auto-exposure in a separate thread that runs at ~30 Hz. Our camd runs at ~8 fps, so AE updates at 8 Hz — slower but sufficient.
- If auto-exposure fails (sensor doesn't respond to register writes), fall back to manual brightness control via the Krux settings page.
EOF
echo "written"
---

## Test results (2026-10-29, recovery mode)

### Verified on device
- **Register write protocol works**: `test_simple.py` sent `W` + 3 bytes, received `b'OK'` (response time < 3s).
- **set_exposure() succeeds**: no error, camd responds OK.
- **set_gain() succeeds**: no error, camd responds OK.
- **camd produces frames**: GOT A FRAME, ~1.9M non-zero bytes per frame.
- **CPP firmware loads**: chromatrix sent (266 words, status 0).

### Not yet verified (device too unstable)
- Whether register writes change image brightness (snapshot returns EAGAIN because
  camd's frame buffer is empty when the snapshot arrives — timing issue).
- Auto-exposure convergence.
- QR scanning with brighter image.

### Device stability notes
- Device crashes within ~10-15 seconds of camd running (thermal).
- Better ventilation helps but doesn't fully prevent crashes.
- Recovery mode (TWRP) is more stable than Android mode for testing.
- camd runs at ~0.02-0.22 fps in recovery (very slow, but functional).
- Killing mm-qcamera-daemon and cameraserver before running camd is essential.

### Test results (2026-10-30, recovery mode, active cooling)

**Verified:**
- Register write protocol works (`test_simple.py`: sent 4 bytes, got `b'OK'`).
- `set_exposure(2000)` succeeds (no error).
- `set_gain(0x0180)` succeeds (no error).
- `camd` produces frames (GOT A FRAME, ~1.9M non-zero bytes).
- CPP firmware loads (chromatrix sent, status 0).
- Snapshot works when camd has produced a frame (Y mean: 0, very dark).

**Not verified (device thermal instability):**
- Whether register writes change image brightness (device crashes before comparison).
- Auto-exposure convergence.
- QR scanning with brighter image.

**Device stability:**
- Device crashes within ~10-15 seconds of camd running.
- Even with active cooling, sustained camera operation is unreliable.
- Recovery mode (TWRP) is more stable than Android mode.
- Short tests (≤10s) work; longer tests crash the device.
