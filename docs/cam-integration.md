# Camera integration: camd ↔ MicroPython/Krux

## Runtime startup

`camd` is now part of the firmware startup, not a manual test step:

- `rootfs/init-stub.c` starts `/data/krux/camd` (or `/opt/krux/camd`)
  before exec'ing MicroPython, with the validated CAMIF_RAW+CPP
  arguments, and waits up to 8 s for `/data/krux/camd.sock`.
- The bundled ramdisk copies `cam/camd` and the CPP firmware into
  `/opt/krux`; `rootfs/build-stub-ramdisk.sh` includes them.
- `scripts/deploy-firmware.sh` pushes the interpreter, `krux/src`,
  `cam/camd`, and the CPP firmware. It does not update the init stub;
  rebuild the stub ramdisk/boot image for automatic startup.
- The test scripts now use `/data/krux/camd` instead of
  `/data/local/tmp/camd`.

## Snapshot path

- `micropython/ports/unix/image_maixpy.c`:
  - `_image.from_nv12` accepts the 8-argument wrapper call from
    `krux/src/image.py`.
  - image buffers use the MicroPython heap (`m_new`) instead of
    `calloc`, so they are collected with the image object.
  - statistics use real luma and expose `l_min`, `l_max`,
    `clipped_low`, `clipped_high`.
- `krux/src/sensor.py`:
  - validates the 32-byte `KCF1` header and ready flag;
  - retries until a new `latest_seq` and drains the frame before
    retrying;
  - caps the client at ~15 fps (`66 ms` period) while the daemon
    produces ~25 fps;
  - `run(0)` closes the socket.
- `micropython/ports/unix/shannon.c` adds the missing
  `shannon.entropy_img16b()` module (RGB565/16-bit histogram).

## Exposure

`camd` has `W` (register write), `E` (exposure/gain), and `V` (VTS)
commands; `sensor.set_exposure_gain()`, `set_vts()` and `__write_reg()`
use them. `Camera._auto_expose()` implements a bounded QR/entropy
convergence loop, but it is **disabled by default**: the validated
runtime binary (`camd-fast`) is the stable unpaced build and does not
include `W/E/V` yet. The fallback therefore uses the fixed vendor
exposure. Enable auto-exposure only after a stable control-capable camd
is built.

## Verified on device

- 10-frame smoke test: all frames 320×240 (control-capable source camd,
  15 fps target).
- Entropy statistics: `l_mean`, `l_stdev`, `l_min`, `l_max`, clip
  counts available (test scene was dark: mean 10, stdev 1).
- `shannon.entropy_img16b` returns a value (`1.5` for a synthetic
  4-pixel buffer).
- QR decode path runs without error (no QR code was in front of the
  camera for the test).
- `camd-fast` unpaced: 512 frames in 20 s (~25.6 fps), 31 ms downscale,
  no CPP stall in that run.
- The 15 fps paced path in the current source can starve the CPP
  (`msm_cpp_cfg` `EAGAIN`); 30 fps can reach the watchdog. Use the
  unpaced `camd-fast` plus the Python 15 fps throttle.
- A later fast-build run on a reused/dirty camera boot produced only
  3 frames and dropped adb; this is the atomic-boot hazard, not a new
  snapshot-path failure.

## Next steps (ordered)

1. Rebuild the stub ramdisk/boot image with the new `krux-init` and
   bundled `camd`; verify a cold boot starts `camd` before Krux.
2. On a clean camera boot, run the full Krux UI: static QR, then an
   animated UR at 5–10 parts/s; record fps, decode success and thermal.
3. Run the entropy page on a well-lit mid-gray scene; verify
   `shannon`, statistics and clipping. Keep the fixed vendor exposure
   until a stable `W/E/V` camd exists.
4. Make the camera boot+run cycle atomic and repeatable; run 10 minutes /
   1,000 snapshots with thermal/watchdog logging.
5. Build a stable control-capable camd or stable paced build; enable
   `Camera._auto_expose()` and the QR/entropy AE policies.
6. Only then evaluate PIX_VIEWFINDER for 20 fps or CPP simplification.
