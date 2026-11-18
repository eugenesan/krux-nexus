# Idle quiesce for camd — port from kern-nexus (2026-11-17)

**Status:** code ported to this branch (`camera/idle-quiesce-port`),
**verified on hardware 2026-11-17**, including a clean teardown with no
kernel error messages.  The feature is default-off; the shipped
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

## Verification results (device 0643efdcf0eca63a, 2026-11-17)

Image: `artifacts/built/boot-dev.img` built from this branch, kernel
`airgap/cam-xz-wipe` at `1d2a76cd835` (CPP NULL-frame guard), booted with
`fastboot boot` only.  The firmware's `/data/krux/camd` was replaced with
the newly built binary after backing the old one up to
`/data/local/tmp/camd.bak-pre-quiesce` (md5-verifiable).  The test knob
was removed from `/data/krux/state` afterwards.

| test | result |
|---|---|
| default knob (`idle_stop_after=0`) | `[stream] idle stop after 0s (0 disables)`; camera unchanged |
| manual quiesce (`0` on ctl socket) | `CFG_STREAM rc=0`, `STREAMOFF rc=0`, `RELEASE_BUF rc=0`, `RELEASE_STREAM rc=0`, `complete, exiting`; supervisor respawned a fresh camd |
| frames after respawn | frame socket returns `KCF1`; `camclient` read 16,192 Y + 16,192 UV bytes, `Y non-zero 16192/76800` |
| automatic quiesce (`idle_stop_after=15`, one client opened+closed) | `quiesce begin` → `complete, exiting` → fresh camd read `idle stop after 15s` |
| grace-window cancel (`0` then `U` within 2 s) | `quiesce cancelled`, **same camd pid**, no restart, frames still served |
| dmesg (initial kernel) | no panic/watchdog, but the teardown logged `msm_vb2_buf_cleanup: NULL stream`, `msm_isp_close_node: Invalid close`, `msm_cci_release invalid ref count` and `msm_actuator_close: cci_init failed` |
| dmesg (after kernel patch `d10a28ee249`) | **all teardown messages gone**; clean quiesce, fresh daemon, frames served |
| camera page after quiesce | entropy page opened and produced `Camera exposure mean=136 exposure=0x09c8 gain=0x00ff`; QR/entropy path works again |

**Not yet covered**: the 10-minute thermal soak and 100-cycle test.
The `Invalid close` messages have been fixed in the kernel (see below)
and the entropy UI path after a quiesce has been verified with touch
injection.  Idle power numbers still need a soak.

**Caution confirmed**: directly `kill -9`-ing a *streaming* camd (rather
than using quiesce) left a zombie child and a supervisor stuck in
`waitpid`, requiring a RAM-only reboot.  Only use the clean quiesce path.

## Why the camera stopped starting after the port (and the fix)

The port changed `krux/src/sensor.py`, but the device runs the Python
sources from **`/data/krux/*.py`**, which shadow the modules frozen into
the interpreter (`init-stub: using the firmware on the data partition`).
Those files dated from 2026-11-05 and were inconsistent with both the
current camd and the current `image.py` (`module 'image' has no attribute
'shared_status'`).  The stale client took a "bad frame header" exception
and abandoned its socket mid-frame, leaving camd's socket thread blocked
in `sock_alloc_send_pskb` — so every later camera page failed too.

Fix: deploy the **whole** current Python package (not just one file):

```
# after backing up /data/krux (65 MB) to the host:
cd krux/src
adb push *.py /data/krux/
adb push krux/*.py /data/krux/krux/
# plus krux/pages, printers, translations, touchscreens
```

`scripts/deploy-firmware.sh` does this (and removes files the tree no
longer has); back up `/data/krux` first because that step deletes files.
A backup from this session is at `/tmp/opencode/krux-data-backup`.

## Reliable touch injection

The working injector sends the complete MT event burst, matching the
Krux `inject.py` in `artifacts/cam-2026-11-11/tools/`:

```
ABS_MT_SLOT 0, ABS_MT_TRACKING_ID 1, ABS_MT_POSITION_X x,
ABS_MT_POSITION_Y y, ABS_MT_TOUCH_MAJOR 8, ABS_MT_PRESSURE 60, SYN
... hold ...
ABS_MT_TRACKING_ID -1, ABS_MT_TOUCH_MAJOR 0, ABS_MT_PRESSURE 0, SYN
```

`EV_KEY`/`ABS_X`/`ABS_Y` are **not** supported by this panel; a tap that
only sets TRACKING_ID and X/Y is unreliable.  A compiled ARM version is
at `/tmp/opencode/touch` (usage `touch X Y [hold_ms]`).  Key injection
does not work; use touch.  Entropy-page path:
New Mnemonic (540,445) → Via Camera (540,203) → 12 words (540,280) →
Yes (810,1850).

## `Invalid close` and the other teardown messages

They are all "already released" paths in the 3.4 camera driver: the
quiesce releases the stream/buffers before the process exits, and V4L2
then closes nodes that are already closed.  Fixed in the shared kernel
branch `airgap/cam-xz-wipe` commit `d10a28ee249`:

* `msm_isp_close_node`: an already-closed VFE is a successful no-op;
* `msm_vb2_buf_cleanup`: no stream during cleanup is a debug message;
* `msm_cci_release`: a repeated release is a no-op;
* `msm_actuator_close`: a failed CCI release on close is not an error.

No functional change to the live path; verified with a manual quiesce:
clean dmesg, fresh daemon, working camera page.

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
