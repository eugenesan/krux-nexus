# Camera bring-up — Stage 1: research and decision

Paste this into a fresh session.  Work only on the camera; do not touch
SeedKeeper or NFC.  Read the files listed here before making changes.

## Goal

Decide and document the camera path before writing port code.  The decision
must be based on measurements, not on the Android HAL3 notes alone.

## Read first, in this order

1. `docs/cam-state.md` — measured V4L2 state; the empty-frame finding.
2. `docs/cam-plan.md` §1, §2, §3, §6 — native V4L2 daemon plan.
3. `docs/cam-bringup.md` — Android Camera HAL3 failure analysis.
4. `docs/cam-bringup-alt.md` — direct HAL3 hypothesis.
5. `tools/profiles/hammerhead-camera.list`
6. `tools/profiles/hammerhead-keep.list` (especially `CONFIG_DEVTMPFS`)
7. `cam/imaging8.c`, `cam/ion.h`, `cam/isp.h`, `cam/ispif.h`,
   `cam/sensor_live.h`, `cam/sensor_config.h`, `cam/v4l2_caf.h`
8. `krux/src/sensor.py`, `krux/src/krux/camera.py`

## Current facts

- The existing V4L2 path reaches `GOT A FRAME`, but the frame has zero bytes.
- The ROM's camera under our kernel shows the same empty frame, so the
  imaging server is not the primary suspect.
- The camera_v2 path is a userspace-event camera: `/dev/video0` is the
  config/event node and `/dev/video1` is the sensor; the kernel blocks until
  our server acknowledges events.
- The kernel built for NFC has `CONFIG_MSM_CAMERA=n` and
  `CONFIG_DEVTMPFS=n`; the camera profile must be used and devtmpfs must be
  enabled for the runtime camera nodes.
- The direct Android HAL3 path in `cam-bringup-alt.md` still depends on
  vendor HAL, gralloc, `mm-qcamera-daemon`, and binder.  That is a different
  compatibility class from the minimal MicroPython rootfs.

## Research tasks

1. Enable devtmpfs for the camera profile.  `hammerhead-keep.list` currently
   forces it off, but the camera nodes are allocated at runtime:
   ```sh
   sed -i 's/^CONFIG_DEVTMPFS=n/CONFIG_DEVTMPFS=y/' tools/profiles/hammerhead-keep.list
   ```
   Then build with the camera profile:
   ```sh
   PROFILE=tools/profiles/hammerhead.list \
       TOOLCHAIN=host \
       bash tools/build-kernel.sh --out /tmp/opencode/ko-cam
   ```
   Restore `CONFIG_DEVTMPFS=n` after the camera kernel if the final air-gap
   image should not expose runtime device nodes.
   Verify the resulting `.config` has `CONFIG_MSMB_CAMERA=y`,
   `CONFIG_IMX179=y`, `CONFIG_MSM_CSID=y`, `CONFIG_DEVTMPFS=y`, and
   `CONFIG_NFC_BCM2079X=y`.

2. Boot it with `fastboot boot` and verify `/dev/video0`, `/dev/video1`,
   `/dev/v4l-subdev11`, `/dev/v4l-subdev3` exist and `get_id()`-equivalent
   sensor access works.

3. Re-run the existing `cam/imaging8.c` path with the measurement fixes from
   `docs/cam-plan.md` §A2:
   - print `planes[0..1].bytesused`, `.length`, `.data_offset`;
   - map each plane over its ION length;
   - save `/data/local/tmp/frame0.nv12`;
   - print `pix_count`/`raw_count`.

4. Decide between:
   - **Path A (recommended first):** native V4L2 + `camd` daemon, as in
     `docs/cam-plan.md` §3.
   - **Path B (fallback only):** direct Camera HAL3 harness, as in
     `docs/cam-bringup-alt.md`.
   Path B is only worth starting if Path A has a documented, measured blocker
   after the kernel registers are compared.

5. If the V4L2 frame is still empty, perform the vendor-register comparison
   from `docs/cam-plan.md` §A4/A5 before changing the userspace design.

## Deliverables

- Update `docs/cam-state.md` with the new kernel build and measurement.
- Update `docs/cam-plan.md` with the chosen path and any changed commands.
- Record the blocker, if any, in `docs/cam-bringup.md`.

## Acceptance

Either a non-zero `planes[0].bytesused` and non-zero Y plane, or a written
explanation of the exact remaining kernel/userspace difference.  Do not start
MicroPython or Krux work until this is settled.
EOF
echo done