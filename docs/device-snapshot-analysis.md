# Device snapshot vs. the project tree

Analysis of `/tmp/opencode/original-data-krux` — a `adb pull` of `/data/krux`
from the original Nexus 5 (`0643efdcf0eca63a`), taken 2026-11-05 17:45.

## Verdict

**Nothing in the device snapshot is newer than the project tree, and nothing in
it contains a fix that is missing from the project tree.**

The snapshot's Python tree is a **byte-exact copy of the `krux` submodule commit
`9bfd7c20`** (2026-11-04 21:11, "camera: stabilise exposure; keep decode in
Krux"). The project tree was 27 commits ahead of that at the time of the
overwrite (`c5b92685`, 2026-11-05 16:37).

The previous session's commit `d1e15529` ("camera: restore the working
camera/QR files from the original device") therefore did the exact opposite of
what it claimed: it overwrote 6 newer project files with 6 older copies and
deleted 27 commits' worth of work (51 insertions, 200 deletions).

## Method

Only files with a counterpart in the project tree were considered. The
deployment layout (`scripts/deploy-firmware.sh`) defines the mapping:

| device path | project path |
|---|---|
| `/data/krux/*.py` | `krux/src/*.py` |
| `/data/krux/krux/**.py` | `krux/src/krux/**.py` |
| `/data/krux/embit/**.py` | `krux/vendor/embit/src/embit/**.py` |
| `/data/krux/flash/**.py` | `krux/src/flash/**.py` |

## Tally

| | count |
|---|---|
| `.py` files in the snapshot | 195 |
| byte-identical to the project tree | 138 |
| differ from the project tree | 1 (`image.py` — project is newer) |
| no counterpart in the project tree | 56 (all ad-hoc `test_*.py` scratch scripts) |

`diff -rq` between the snapshot's firmware tree and `9bfd7c20` reports
**zero differing files**.

### The 56 files with no counterpart

All are `test_*.py` debug harnesses that were `adb push`ed onto the device
during the AF/AE/exposure sessions and were never committed:

```
test_ae.py .. test_ae15.py      test_exp.py, test_exp_pairs.py, test_exp_steps.py
test_brightness*.py             test_cam.py, test_cam_stream.py
test_dec.py, test_dec2.py, test_dec_vga.py
test_dir.py, test_display.py, test_exposure.py
test_fifo.py, test_flip.py      test_lcd.py, test_lcd2.py
test_pair.py, test_payload.py, test_perf.py
test_profile*.py                test_qr.py, test_qr2.py, test_qr_save.py
test_reg.py, test_reg2.py       test_save.py, test_scale.py
test_seq.py, test_shr.py, test_simple.py
test_stream_long.py, test_two.py, test_vga_long.py, test_vga_loop.py
test_vts.py, test_consistency.py
```

These are throwaway probes, not firmware. They contain no code that the
firmware needs. The only thing worth keeping from the device that is *not* in
the tree is the `camd` binary and the `camd*.log` transcripts, and both are
already in the project (`cam/camd`) or are regenerable.

### The one file that differs — `image.py`

```diff
-    def get_statistics(self, *a, **k):
+    def get_statistics(self, roi=None):
+            if roi:
+                return self._native.get_statistics_roi(
+                    roi[0], roi[1], roi[2], roi[3]
+                )
```

The **project tree is newer**. The ROI statistics came in with `0422354c`
(2026-11-05 10:45, "camera: ROI AE, BBQR deflate compatibility and startup
timing"). The device's copy predates it. Pulling the device file would delete
the ROI support the AE controller depends on.

## Per-file findings for the 6 files the overwrite touched

### `src/sensor.py` — device is 1 day older

Device copy = `69a3fbda` (5968 bytes). Project before the overwrite =
`7688d71f` (6914 bytes). The overwrite deleted:

- `refocus(full=False)` — the `R`/`r` socket command that runs the AF sweep.
  Without it there is no way to refocus at scan start or on Power.
- `dump()` — the `D` socket command that makes camd write the preview and
  source frames for HD-QR analysis.
- `snapshot(timeout=20000)` — the configurable timeout. The device version
  hard-codes 20000 ms, so `qr_capture.py` can no longer ask for a short
  500 ms poll.
- the `_frame_count % 5` GC throttle — the device version calls `gc.collect()`
  on **every** frame, which is the 1,382,400-byte preview `MemoryError`
  pressure.
- the 20 ms/20 ms back-off sleeps. The device version polls at 2 ms/1 ms,
  hammering the frame socket when camd has no frame.

### `src/krux/camera.py` — device is 19 commits older

Device copy = `9bfd7c20` (17597 bytes). Project before the overwrite =
`c5b92685` (20346 bytes). The overwrite deleted:

- `_disable_cpp_dump()` — writes `0` to
  `/sys/module/msm_cpp/parameters/cpp_dump_frames` at camera init. The
  64-frame CPP debug dump stalls the pipeline; `390a306e` added this.
- `adjust_exposure(delta)` — the manual exposure nudge (`_ae_manual` lock).
  `82f07bfc` / `bff31975` added it.
- `refocus(full=False)` and `dump()` — the `Camera`-level wrappers.
- `auto_expose(mode)` — the public one-shot AE entry point.
- the `_ae_manual` guard in `_auto_expose()` and `auto_expose_step()` — so
  the AE can no longer be locked by the user.
- `MAX_EXPOSURE` `0x09C0` → `0x09D6` (the brighter QR target from
  `9cb6b8ba`).
- the **IMX179 gain-range fix**: the device version starts `gain = 0x0100`
  and caps at `0x01FF`, but the register is 8-bit in this mode, so
  `0x0100` lands as `0x00` and `0x01FF` as `0x00FF` — the gain could never
  actually be raised. The project version starts at the vendor default
  `DEFAULT_GAIN` and caps at `0x00FF`.
- the AE target `0x68` → `0x80` for the QR mode, and the entropy-mode
  special case.
- the AE convergence loop `14` → `8` iterations and the `5000` ms snapshot
  timeout on each step.
- `initialize_run()`: the "run the one-shot AE **only on the first camera
  page**" guard (`c5b92685`). The device version re-runs the AE on every
  camera page, which was the cause of "QR works only the first time".
- `initialize_run()`: the `_disable_cpp_dump()` call.
- `snapshot(timeout=20000)`.
- `stop_sensor()`: the comment/behaviour note that camd now sees no frame
  requests and skips the expensive per-frame work while idle.

### `src/krux/pages/qr_capture.py` — device is 20 commits older

Device copy = `9bfd7c20`. Project before the overwrite = `ae505163`. The
overwrite deleted:

- the `initialize_run()` try/except that shows **"Camera not ready / Let it
  cool, then retry"** instead of an unhandled traceback.
- the `refocus()` at the start of every scan (`7fd7ec1d`) — without it a
  second scan silently reuses a stale focus.
- the on-screen **−/+ exposure buttons** and the **Vol+/Vol− exposure
  nudges** (`bff31975`, `bd3cff62`).
- the **Enter-key full AF sweep** and the **centre-tap refocus**
  (`9e881dd1`).
- the `snapshot(timeout=500)` short poll — the device version uses the
  20 s default, so a blank wall freezes the loop instead of letting Power
  refocus.
- the snapshot-failure tolerance `3` → `20` and the 100 ms → 200 ms back-off.
- the preview redraw cadence `frame_count % 3` → `% 2`.
- the **"TAP exit  VOL+/- exp  PWR focus"** footer (replaced with
  "TOUCH or VOL+/VOL- exit").
- the `kboard.is_hammerhead` guards around the portrait/landscape
  progress-bar redraws.

### `src/krux/bbqr.py` — device is 2 commits older

Device copy = `9bfd7c20`. Project before the overwrite = `0422354c`. The
overwrite deleted:

- **`deflate.DeflateIO(stream, deflate.ZLIB)` → `deflate.DeflateIO(stream)`.
  This is the BBQR interop fix.** BBQR uses zlib-wrapped deflate; MicroPython's
  default `AUTO` writes *raw* deflate, which other wallets cannot read.
  `0422354c` fixed it and it was verified with the 20 KB / 33-part synthetic
  decode.
- the **`AUTO` → `RAW` → `ZLIB` decompress fallback** in
  `deflate_decompress()`; the device version only tries `AUTO`.
- the `except Exception as e` → `except:` change, which dropped the error
  message from the raised `ValueError`.

### `src/krux/baseconv.py` — device is 1 commit older

Device copy = `9bfd7c20`. Project before the overwrite = `b5249b9e`.

```diff
-        from . import base32_compat as base32
+        import base32
```

This is a **hard breakage on this port**, not a style difference. The
`unix`/arm32 MicroPython port has **no `base32` module** — verified against
the built `rootfs/micropython-arm32` (only `binascii` and `hashlib` are
present). `krux/base32_compat.py` was added by `b5249b9e` precisely to fall
back to the pure-Python `krux/_base32.py` when the native module is absent.
Reverting to `import base32` raises `ImportError` the first time a base32
QR is encoded or decoded.

### `src/krux/power.py` — device is 1 commit older

Device copy = `9bfd7c20`. Project before the overwrite = `4d7a5da1`.

```diff
+            self.pmu.enter_sleep_mode()
             machine.reset()
```

The project **removed** the sleep on purpose: the memory-wipe path kexecs
from `machine.reset()`, and suspending first means the kexec handoff is
never reached. Reverting it breaks the verified `wipe=1` (wipe and power
off) behaviour.

## Conclusion and required corrections

1. **Revert `d1e15529` in the `krux` submodule** to restore `c5b92685`'s
   version of the 6 files. Nothing from the device snapshot should be pulled
   in.
2. **Delete `deploy/working-krux/` and `scripts/deploy-working-krux.sh`.**
   They are a copy of the *old* `9bfd7c20` tree sitting in the project tree;
   running that script would deploy 27 commits of stale code — the exact
   inverse of "code on the device should never be newer than the project
   tree". The project tree is the source of truth; git history already
   preserves `9bfd7c20` if it is ever needed.
3. **Deploy with `scripts/deploy-firmware.sh`**, which pushes `krux/src` +
   `rootfs/micropython-arm32` + `cam/camd` — the only supported path.
4. The 56 scratch `test_*.py` scripts need no action. If the AF/AE
   experiments need to be repeated, they should be committed under
   `probes/` so they are not lost, rather than left on a device.

## Timeline of the failure

| when | what |
|---|---|
| 11-04 21:11 | `9bfd7c20` — the tree the device was running |
| 11-05 07:57 – 16:37 | 27 further commits: power wipe path, IMX179 gain fix, base32 fallback, BBQR ZLIB fix, ROI AE, full-res snapshot, high-density QR limit, manual exposure, refocus at scan start, frame dump, CPP dump disable, AE-once |
| 11-05 17:45 | `adb pull` of the device's `/data/krux` — an untouched copy of the 11-04 tree |
| 11-05 17:57 | `d1e15529` overwrites 6 newer project files with the 11-04 copies |

The device was simply never re-deployed after 11-04 21:11. The snapshot is
a stale artifact, not a newer source of truth.

## Two more traps found while clearing this up

### The bundled `/opt/krux` silently shadowed `/data/krux`

There are two ways to put Krux on a device:

- `/data/krux` — pushed over adb by `scripts/deploy-firmware.sh`.  Writable,
  and the only place a code change is ever made.
- `/opt/krux` — a read-only copy baked into the boot image
  (`build-stub-ramdisk.sh --bundle`).

`rootfs/init-stub.c` chose between them and **preferred `/opt/krux`**.  So on
a bundled image every `deploy-firmware.sh` push had no effect at all: the
device kept running the code frozen into the image.  That is what made the
earlier sessions conclude that the deployed code was "experimental" and
broken, and it is why the device's `/data/krux` looked like the only thing
that worked.

Proven on the device: a marker line was added to the deployed
`/data/krux/boot.py`, and the firmware printed the stub's other message --
`using the firmware bundled in the boot image` -- instead.  The running
interpreter was confirmed with `/proc/<pid>/exe -> /data/krux/micropython`
after the precedence was flipped.

The precedence is now inverted: `/data/krux` wins, `/opt/krux` is the
fallback for a device with no development tree of its own.  The development
boot image is built without `--bundle`, so there is exactly one firmware
copy on the device and no possibility of confusion.

### `cam/camd` did not match `cam/camd.c`

The commit that reverted the 293-word CPP experiment (`f2ec8eb`) restored the
source but never rebuilt the binary, so `cam/camd` was still the
293-word build.  The device was therefore running a daemon that sent the
wrong CPP message: the firmware loaded it, the driver clamped it, and the
CPP command queue grew while every output buffer stayed empty
(`bytesused=0`, `CFG: Resource temporarily unavailable`).

The daemon is now rebuilt from the source, and `scripts/deploy-firmware.sh`
rebuilds it automatically whenever `cam/camd.c` is newer than `cam/camd`, so
the two cannot drift apart again.

`scripts/cam/build-ae-camd.sh` and `tools/ae-patch.py` were removed for the
same class of reason: the AE socket commands are in the committed source, and
the patcher's pattern still matched it, so `--apply` *duplicated* the AE
block instead of adding it.  `scripts/cam/build-camd.sh` builds the daemon
straight from `cam/camd.c`.

