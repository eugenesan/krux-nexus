# Idle quiesce for camd — port from kern-nexus (2026-11-17)

**Status:** code ported to this branch (`camera/idle-quiesce-port`), compiles,
**not yet device-verified**.  The feature is default-off; the shipped
continuous-stream behaviour is unchanged unless the knob is set.

## What this is

A clean stop/restart of the camera pipeline, ported from the working
solution in `kern-nexus` (`230a0e8`..`133397e`, `925d4d3`, hardened in
`5e3e4f6`, documented in `docs/STAGE1-CAMERA-IN-PROCESS.md` there).
The earlier on-demand experiment (long `0x0100` sensor pause, reverted
2026-11-12) failed because the ISP/CPP came back out of frame sync.  This
version instead tears the **whole pipeline** down in the only order this
kernel recovers from, exits the camera child, and lets the init-stub
supervisor start a fresh daemon on the next camera page.

Ported pieces:

| kern-nexus | krux-nexus |
|---|---|
| `KERN_CAMERA_IN_PROCESS` quiesce in `camd.c` | `krux_camera_quiesce()` in `cam/camd.c`, ungated by build flag, runtime-gated by the knob |
| in-process log ring | standard camd logging to `/data/krux/camd.log` (init-stub already redirects stdout) |
| SIGUSR2 cancel from the parent | frame-socket `U` command (MicroPython-compatible) **and** SIGUSR2; a new client connecting during the grace window also cancels |
| parent respawn via `app_video_start()` | existing `rootfs/init-stub.c` supervisor (`waitpid` → `sleep(1)` → new `fork`/`execl`) |
| kernel CPP NULL-frame guard | cherry-picked from `airgap/cam-xz-wipe-kern` (`3e3f2ace562`) onto `airgap/cam-xz-wipe` as `1d2a76cd835` |

Not ported (not needed for stop/restart): in-process camd/exec of
`/proc/self/exe`, shm frame reads, 50 ms pacing, precomputed crop maps.
Those are kern-specific performance work.

## Knob

```
/data/krux/state/idle_stop_after   seconds, 0 = disabled (DEFAULT)
```

Read at camd startup via `state_read_persist("idle_stop_after", 0, 0, 3600)`.
Trigger requires: knob > 0, `idle > 3` frame periods, at least one past
frame request, **zero live frame-socket clients**, and no request for
`idle_stop_after` seconds.  A page opening during the 15 s teardown grace
window cancels the quiesce entirely and resets the idle timer.

## Teardown order (do not reorder)

1. `VIDIOC_MSM_ISP_CFG_STREAM` with `STOP_STREAM` on `isp_stream_handle`.
2. `VIDIOC_STREAMOFF` on the capture node (`/dev/video1`, fd saved at open).
3. Sensor stream bit off: `0x0100 = 0` (`stream_off_regs`).
4. `VIDIOC_MSM_ISP_RELEASE_BUF` with `isp_buf_handle`, `ISP_SHARE_BUF`.
5. `VIDIOC_MSM_ISP_RELEASE_STREAM` with `isp_stream_handle`.
6. Unlink both socket paths, then `_exit(0)`.

The kernel guard (`msm_cpp_do_timeout_work()` NULL-frame check) is
mandatory: a teardown can leave a CPP frame timer armed after its frame
was consumed, and without the guard the kernel panics.  Do not test
quiesce on a kernel without it.

## Client changes

`krux/src/sensor.py`:

* `reset()` (called by `Camera.initialize_sensor()` on every camera page)
  sends `U` on the frame socket to cancel an in-flight quiesce.
* `_connect()` retries 60×100 ms (was 30) so a supervisor respawn (~1 s
  plus cold init) can complete before the page gives up.
* Existing `image.shared_reset()` on reset already handles the new
  daemon's recreated shared regions.

## Verification plan (before enabling by default)

1. Boot an **audit** image with `fastboot boot` only; keep the known-good
   images and `/data` intact.  Kernel must be `airgap/cam-xz-wipe` with
   commit `1d2a76cd835`.
2. With `idle_stop_after=0`, verify the current camera still works
   (regression check — this is the shipped behaviour).
3. Set `idle_stop_after=15`, open a camera page, decode a QR, leave the
   page, watch `/data/krux/camd.log` for `quiesce begin` → `complete,
   exiting` and the supervisor's fresh `camd is ready`.
4. Reopen the camera page: socket connect during grace → `quiesce
   cancelled` (no restart), or after exit → respawn + reconnect.  Verify
   QR decode, entropy bits/pixel and an AF sweep.
5. Race test: reopen exactly as `quiesce begin` appears; the page must
   come up with one clean preview, not a blank one.
6. Soak: 10 minutes idle (thermal/current), 100 page cycles, and check
   dmesg for the failure signatures below.

**Stop conditions** — abandon and roll back on any of: `No free write
masters`, `msm_isp_close_node: Invalid close`, `Write master ping pong
mismatch`, `Tasklet queue overflow`, `wdog_bark`, `msm_cpp_do_timeout_work`
panic, or drifting horizontal overlap in the preview.

## Rollback

* Remove/zero `/data/krux/state/idle_stop_after` → continuous stream,
  exactly the previous behaviour.
* Known-good pre-port points: Krux `camera-good-2026-11-12` tag, camd md5
  `99ef6da3275fdb882efe9cbf18ed67fe`, kernel `airgap/cam-xz-wipe` at
  `cc17c59739b`.  The branch `camera/idle-quiesce-port` holds the port;
  `master` is untouched.
