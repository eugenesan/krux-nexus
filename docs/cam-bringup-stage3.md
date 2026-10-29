# Camera bring-up — Stage 3: MicroPython and Krux integration

Paste this into a fresh session.  Start only after Stage 2 has produced a
non-empty NV12 frame and the exact stream source is chosen.

## Goal

Make Krux's camera pages work on the air-gapped phone using the validated
native capture path.  Keep the camera daemon separate from MicroPython and
keep the change set small enough to review.

## Read first

1. `docs/cam-bringup-stage2.md`
2. `docs/plan-camera.md` §3 and §4 (Phases B, C, D)
3. `docs/cam-state.md`
4. `krux/src/sensor.py`
5. `krux/src/krux/camera.py`
6. `krux/src/krux/pages/capture_entropy.py` and the QR capture page
7. `micropython/ports/unix/Makefile`
8. `scripts/deploy-firmware.sh`
9. `scripts/build/build-micropython.sh`

## Architecture to implement

Use the daemon split from `docs/plan-camera.md` §3:

- `cam/camd` owns `/dev/video0`, `/dev/video1`, sensor, CSID/PHY, ISP-IF,
  ISP, and CPP only for the raw path.
- `camd` delivers QVGA frames over `/data/krux/camd.sock`.
- `krux/src/sensor.py` is a thin MaixPy-compatible client of that socket.
- `krux/src/krux/camera.py` should need no page-level changes for the first
  landing.

Do not embed the V4L2 state machine in MicroPython.

## New and changed files

- `cam/camd.c` (new daemon)
- `krux/src/sensor.py` (replace the stub)
- `micropython/ports/unix/image_maixpy.c` (native `_image`)
- `micropython/ports/unix/camhost_linux.c` (`start`/`stop` via
  `fork`/`execv`/`waitpid`)
- `krux/src/krux/camera.py` (minimal upstreamable patch only if required)
- `scripts/deploy-firmware.sh` (push `camd`)
- `scripts/build/build-micropython.sh` (register new C modules)
- tests under `krux/tests/` for `_image` and sensor behaviour

## Required sensor API

Implement `docs/plan-camera.md` §B4 exactly:

- `reset(freq=..., dual_buff=False, choice=None)`
- `get_id()` -> `0x179`
- `set_pixformat(f)` for `GRAYSCALE` and `RGB565`
- `set_framesize(f)` for QVGA
- `set_windowing(roi)`
- `set_hmirror(v)`, `set_vflip(v)`
- `skip_frames(num=10, time=0)`
- `run(enable)`
- `snapshot()` -> never `None`
- `close()`
- `__write_reg(r, v)` and `_Camera__write_reg(r, v)`

Module-level functions must forward to one singleton.  Do not leave the
current per-call `sensor()` construction in place.

## Native image requirements

Implement `docs/plan-camera.md` §C1 and §C2:

- NV12 to QVGA conversion in C, never through Python lists;
- no 1640x1232 frame on the MicroPython GC heap;
- `find_qrcodes()` using vendored `quirc`;
- RGB565 little-endian `to_bytes()`;
- `get_statistics()` and `get_histogram().get_threshold().value()`;
- enough compatibility for `tiny_seed`, `qr_view`, `flash_tools` and
  `capture_entropy`.

## Build and deploy

```sh
arm-linux-gnueabi-gcc -O2 -static -march=armv7-a -mfpu=neon-vfpv3 \
    -mfloat-abi=hard -Icam -o /tmp/opencode/camd cam/camd.c -lpthread
bash scripts/build/build-micropython.sh
DEPLOY_RESTART=1 bash scripts/deploy-firmware.sh
# push camd to /data/krux/camd as part of deploy
```

Deploy the rebuilt kernel with `fastboot boot`, not `fastboot flash`.

## Validation

Use `docs/plan-camera.md` §D1-D5.  At minimum:

```sh
adb shell 'cd /data/krux && ./micropython -c "
import sensor, image
sensor.reset(); print(hex(sensor.get_id()))
sensor.set_pixformat(sensor.RGB565); sensor.set_framesize(sensor.QVGA)
sensor.run(1)
for i in range(10):
    img = sensor.snapshot(); print(i, img.width(), img.height())
sensor.run(0)"'
```

Pass: ten distinct 320x240 frames, no `SensorError`, clean `dmesg`, and
`camd` exits when the page closes.

## Constraints

- No modem, Wi-Fi, Bluetooth, vendor HAL, or Android framework in the final
  path.
- No camera daemon running when no camera page is open.
- No GC-heap allocation of full frames.
- Keep the change set minimal and upstreamable.
- Do not commit card-content changes; the card must not be modified during
  camera work.

## Acceptance

- QR capture page decodes a code at 15-25 cm.
- Entropy page returns scene-dependent statistics.
- 1,000 snapshots without an oops, reboot, fd leak, or >8 KB GC loss.
- Air-gap checks pass: no new interface, socket, or module after a camera
  session.
EOF
echo done