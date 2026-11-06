# Camera AF/AE — technical brief for external review

> Status: 2026-11-06. Everything below is the *current* state of the tree
> (`krux` at `653d52a9`, `cam/camd.c`, MicroPython at `cd318f145`) and of
> the running device (`0879524b0068abca`, binned rear camera).
>
> The purpose of this document is to get a second opinion on the
> auto-exposure and autofocus design, because the current one is
> fighting the sensor rather than the scene.

---

## 1. The platform

| | |
|---|---|
| SoC | Qualcomm **MSM8974** (hammerhead / Nexus 5), kernel 3.4.113 |
| Sensor | Sony **IMX179**, rear camera, driven directly over I2C (no vendor HAL) |
| Sensor mode | **rear-binned**, 1640×1232 source, `HMAX=0x0d70`, `VMAX=0x04e4` (1252 lines) |
| Capture | CAMIF_RAW → offline CPP → V4L2 → userspace `camd` daemon |
| Daemon | `cam/camd.c`, a C daemon that owns the V4L2/CPP state machine and publishes frames over a UNIX socket at `/data/krux/camd.sock` |
| Firmware | MicroPython (unix port) on the device; the QR page is `krux/src/krux/pages/qr_capture.py` |
| Decoder | **quirc** (forked as `k_quirc`) compiled into the MicroPython image module; `image.find_qrcodes()` |
| Preview | Downscaled to **720×960** portrait (rotated 90° CCW from the 1640×1232 source), box-filtered, optional unsharp mask |
| Frame rate | The daemon delivers ~15–25 fps; the QR loop throttles itself to ~15 fps |

### Sensor registers that matter

| Register | Meaning | Value in use |
|---|---|---|
| `0x0202`/`0x0203` | Coarse integration time ("exposure"), in **lines** | 16-bit write |
| `0x0204`/`0x0205` | Analog gain | 16-bit write |
| `0x0340`/`0x0341` | **VMAX** — frame length, in lines | `0x04e4` (1252) |

Register writes go through the kernel's `sensor_cfg()` I2C path. Exposure
and gain are written as a single 16-bit `WORD_DATA` pair each, because
two separate byte writes leave the value half-applied.

---

## 2. What the AE does today

### The measurement

`Camera._stats()` takes the **middle half** of the frame (ROI
`w/4, h/4, w/2, h/2`) and calls the *native* `img.get_statistics_roi()`.
(A previous version called the Python wrapper `img.get_statistics(roi)`,
which does not exist on this port's native image object — the call
silently fell back to whole-frame statistics. That bug cost a day and is
the reason the native ROI path exists now.)

From that ROI the AE reads **`l_bright_mean`**: the mean of all pixels
that lie within **40 levels** of the ROI's brightest pixel
(`BRIGHT_MARGIN` in `image_maixpy.c`). Rationale: a QR code on a screen
is a small bright thing in a usually dark picture, so a mean over the
frame says more about the room than about the code. The bright-pixel
mean also survives (a) the screen's own PWM rolling bars and (b) an
animated code whose frames carry different black/white mixes but the
same white level.

Target: **`BRIGHT_TARGET = 225`** (the code's white should sit just
under the clipping point at `SATURATED = 250`).

### The controller

`Camera.auto_expose_step()`, called from the QR loop on **every third
frame** via `auto_expose_follow()`:

1. Skip if the user has manually adjusted exposure (`_ae_manual`), or if
   an AF sweep is running (`sensor.sweeping()` — sharpness scores are
   only comparable at a fixed exposure).
2. Compute `factor = sqrt(target / mean)`, clamped to **[0.6, 1.6]**.
   The square root is deliberate damping: the picture is not exactly
   linear in exposure, so the exact factor overshoots and the picture
   swings between white and black.
3. `step_down = max(0x40, exposure * (1 - factor))`,
   `step_up = max(0x40, exposure * (factor - 1))`.
4. Act in a fixed order:
   - **too bright** (`mean >= 250`): digital gain down first → exposure
     ×0.7 → analog gain down by `0x18`.
   - **too bright** (`mean > target+16`): digital gain down → exposure
     down by `step_down` → analog gain down by `0x18`.
   - **too dark** (`mean < target-16`): **exposure up first**, then
     analog gain up by `0x18`.
   - Digital gain is **never** used to brighten: it lifts the noise with
     the signal and pins the measurement.

Bounds: `MIN_EXPOSURE=0x0100`, `MAX_EXPOSURE=0x09d6`, `MIN_GAIN=0x0060`,
`MAX_GAIN=0x00ff`.

### The digital gain

`luma_gain` (1–4) is a **software multiply applied by `camd` to the
luma plane after downscaling**. It exists because the IMX179's analog
gain tops out at `0xff` in this mode, and a dim scene can run out of
brightness with the analog settings alone. It is forced to 1 for the
entropy capture (it would reshape the picture being measured).

### The AF

`camd` owns a VCM actuator (DW9714) and runs a sweep:

- **Metric**: `af_sharpness()` — the sum of |horizontal gradient| +
  |vertical gradient| over the middle half of the frame, every 2nd
  pixel. It is compared *within one exposure* only.
- **Coarse pass**: steps 0, 8, 16, … 44 (8 at a time), 4 settle frames
  per step.
- **Fine pass**: ±5 steps around the coarse peak, 3 settle frames.
- **Final**: a one-step preload move, then a move to the best step —
  this is deliberate, to land the VCM on the same side of the backlash
  as the measurement.
- `last_focus_step` is cached; a later scan does a ±5 local sweep
  around it and falls back to a full sweep if that is weak.
- `af_best_contrast` (sharpness ÷ mean) records how sharp the best
  position was, so a scan that finds the picture already that sharp
  skips the sweep entirely.
- Any change of exposure or gain **aborts** a running sweep
  (`af_abort`), because the scores would no longer be comparable.

**Sweep scheduling (firmware side):** there is deliberately **no sweep
at page open** — the user aims at the code after the camera comes up,
so a sweep then would measure the wrong thing at the wrong exposure.
Instead a sweep is requested when the code has been unread for **≥15
frames**, the picture is `picture_settled()` (within `SETTLED_TOLERANCE=18`
of target and not saturated), **at most 2 sweeps per scan**, and never
while one is running. Tap = full refocus; PWR (== ENTER) leaves the
camera; `−`/`+` strip and Vol± adjust exposure manually.

---

## 3. The problems

### 3.1 The exposure ceiling is unreachable — the sensor clamps it (PRIMARY BUG)

`MAX_EXPOSURE` is `0x09d6` = **2518 lines**. The sensor's VMAX in this
mode is `0x04e4` = **1252 lines**, and the legal integration time is
**VTS − 4 = 1248 lines**. `camd`'s own register read-back confirms the
sensor is running with `0x0340/0x0341 = 0x04e4`:

```
[sensor] register read-back (after stream-on):
  0x0340 = 0x0004   0x0341 = 0x00e4     <- VMAX = 0x04e4 = 1252
  0x0202 = 0x0004   0x0203 = 0x00e0     <- exposure = 0x04e0 = 1248
  0x0204 = 0x0000   0x0205 = 0x00e0     <- gain = 0x00e0
```

So **every exposure the AE commands above `0x04e0` is silently
discarded by the sensor**. The AE's usable range is really
`0x0100`–`0x04e0`, not `0x0100`–`0x09d6`. The top half of the
controller's authority simply does not exist.

Measured on the device (fixed gain, `0x0400`; sweeping exposure):

```
 exp     gain    mean   max  bright_mean  n_bright
0x0400 0x0060    15.6    23      15.6   86400
0x0400 0x0080    15.6    24      15.6   86400
0x0400 0x00a5    15.6    24      15.6   86400
0x0400 0x00c0    15.6    29      15.6   86400
0x0400 0x00ed    16.3    35      16.3   86400
0x0400 0x00ff    33.7   147     116.8      79
```

and

```
 exp     gain    mean   max  bright_mean  n_bright
0x0100 0x00a5    15.6    19      15.6   86400
0x0200 0x00a5    15.9    23      15.9   86400
0x0400 0x00a5    15.9    23      15.9   86400
0x0600 0x00a5    17.1    32      17.1   86400
0x0800 0x00a5    16.2    26      16.2   86400
0x09d6 0x00a5    15.3    25      15.3   86400
```

Two things to note, both of which are measurement problems *and* real
findings:

- The scene during this test was a **dark desk** (mean ≈ 15). The
  numbers are therefore mostly sensor noise, not signal.
- The **bright-pixel count is 86400 = every pixel** at low levels.
  `BRIGHT_MARGIN = 40` means "within 40 levels of the brightest pixel",
  so in a scene whose brightest pixel is below ~40 the threshold goes
  to zero and *every* pixel counts as "bright". The bright mean then
  degenerates into the plain mean. **The metric has no dynamic range in
  dark scenes** — exactly the scenes where the AE is supposed to be
  working hardest.
- The **gain response is strongly non-linear**: `0x0060`→`0x00c0` moved
  the mean by 0.0, `0x00ed` by 0.7, `0x00ff` by **18**. The AE treats
  gain as a smooth monotonic knob and steps it by `0x18`; the actual
  response is a cliff near the top of the range. An AE that walks
  gain in `0x18` steps across that cliff will overshoot badly or stall
  just below it.

### 3.2 The controller has no idea the sensor clamps

Because the AE believes its ceiling is 2518 lines, it will happily
command `0x09d6`, see **no change** in the picture (the sensor ignored
it), and conclude it needs to go *further* — which it cannot. The
observed symptom is exactly this: pointing away from the code at a
darker scene sends the exposure to the ceiling and it stays there.

The device's own AE log from the last session shows the failure mode
directly:

```
Camera AE mean=15 clipped=0 exposure=0x0999 gain=0x00a5
Camera AE mean=15 clipped=0 exposure=0x09d6 gain=0x00a5
Camera AE mean=15 clipped=0 exposure=0x09d6 gain=0x00bd
Camera AE mean=15 clipped=0 exposure=0x09d6 gain=0x00d5
Camera AE mean=15 clipped=0 exposure=0x09d6 gain=0x00ed
Camera AE mean=244 clipped=36752 exposure=0x0972 gain=0x00ed
```

The exposure climbed 0x0999 → 0x09d6 with the mean frozen at 15 (the
sensor was already clamped at 0x04e0), then the gain crept up in
`0x18` steps. The whole first half of that trace is the controller
pushing on a door that is welded shut. Note also `mean=244` with
`clipped=36752` — the "bright" pixels are saturated *and* the count of
them is large, so the bright mean is a saturated quantity and the
`SATURATED` threshold (250) is only just above it.

### 3.3 The AE and the AF are coupled through the frame, and it deadlocks

`af_abort` cancels a running sweep whenever the exposure or gain
changes. The AE runs on every 3rd frame; the sweep needs 4 settle
frames per coarse step plus a fine pass. So:

- AE changes exposure → sweep is aborted → sweep restarts → AE changes
  exposure again → …
- The firmware's mitigation is to freeze the AE while `sensor.sweeping()`
  is true (`hdr[18]` carries the sweep flag in every frame header).

But `hdr[18]` is only true *while* a sweep is running, and the sweep is
restarted by the AE step that happens to land between sweeps. With a
15-frame "unread" trigger and 2 sweeps allowed, the sweep frequently
loses its race with the AE and the picture never settles enough for
`picture_settled()` to become true, so no sweep is even requested.

### 3.4 The decoder thresholds globally; ZXing thresholds locally

The `k_quirc` fork does **Otsu over the whole frame** (with a 20%
border margin) and applies one global threshold. The bilinear
(per-quadrant) threshold path exists in the source
(`K_QUIRC_BILINEAR_THRESHOLD`) but is **not compiled in** — neither it
nor `K_QUIRC_ADAPTIVE_THRESHOLD` is defined in the build.

ZXing's `HybridBinarizer` computes a **local 8×8-block** threshold with
a 5×5 neighbourhood average. That is the other half of "detects
instantly regardless of lighting": the camera's AE and the decoder's
binariser are *both* global, so a bright code in one corner of the
frame is binarised against a threshold computed for the whole frame.
This is the single biggest remaining gap versus a reference
implementation.

### 3.5 The picture is a third of the source resolution

The preview is 720×960 from a 1640×1232 source — the code is decoded
at roughly **1/3 of the pixels per module**. A dense (HD) code has
modules only a few pixels wide at 720×960; at 1640×1232 the same code
has ~2× the linear resolution. The full-resolution path existed
(`sensor.snapshot_full()` + camd's `H` command, in `16fa1cd`/`2d69a789`)
and was removed in `3b5761d`/`f8cce078` as collateral damage from a
preview-stability change — **not** because it was judged unworkable.

The source is *already* in the daemon: `camd` downsamples it into
`latest_y` on every frame, and it also writes the undownsampled source
to `/data/krux/scan_full.raw` when asked to dump.

### 3.6 The picture is monochrome, and the chroma plane is not NV12

`from_nv12` packs the luma into all three channels as greyscale. The
chroma plane that the CPP writes is **not** the NV12 that a YUV
conversion assumes: its values are centred on **16**, not 128 (mode 16,
range 9–45), so converting it turns the whole picture green. The chroma
is therefore read and dropped.

What *was* fixed and is worth keeping: the image object now keeps the
sensor's **own luma plane**, and both the decoder and the statistics
read that, instead of reconstructing luma from an RGB565 picture whose
R and B channels are 5 bits. The round trip cost up to 5 levels/pixel
(measured: mean 2.5, 51% of pixels off by more than two). This matches
what ZXing does — `PlanarYUVLuminanceSource` "uses Y channel directly
without RGB conversion".

### 3.7 Noise in dim scenes

The delivered frame is genuinely noisy at low light — a 16×16 patch
reads `35 33 23 27 80 43 24 44 20 33 21 26 40 14 26 10`. That is
sensor noise, not a downscale fault. At that noise level the black
modules of a dense code float above the threshold and the code will not
decode. This is a signal problem that no exposure tuning fully fixes.

---

## 4. Constraints any fix must respect

- **No modem, no Wi-Fi, no Bluetooth, no vendor HAL.** The sensor is
  driven directly over I2C; the CPP firmware is loaded by the kernel.
- MicroPython, on a 1 MB default GC heap (32 MB via `-X heapsize=`).
  The QR page must keep up with the decoder at ~15 fps.
- **No symlinks** in the Krux tree; the project tree is the single
  source of truth and the device must never be newer than the tree.
- **Never flash.** `fastboot boot` only. A cold boot is needed for a
  clean camera session (the CPP pipeline cannot be torn down and
  recreated reliably within one boot).
- Temporary camera state lives in `/tmp/krux/{af,focus,luma_gain,unsharp}`,
  is optional, and is reinitialised with defaults when missing.
- The entropy capture must not have the picture reshaped under it: the
  digital gain is forced off and the manual exposure lock is ignored in
  `ENTROPY_MODE`.

---

## 5. Specific questions for the reviewer

1. **Exposure ceiling.** Given that the sensor clamps integration time
   to VTS−4 = 1248 lines and the AE thinks its range ends at 2518, is
   the right fix to (a) clamp `MAX_EXPOSURE` to `0x04e0` and raise VMAX
   to 2518 lines for longer exposures, (b) clamp `MAX_EXPOSURE` to
   `0x04e0` and accept 1248 lines as the ceiling, or (c) something
   else? Raising VMAX halves the frame rate to ~15 fps, which the
   preview budget may not absorb. Note the device crashed when VMAX was
   changed at runtime over the socket (see §6) — that may be a
   camd/kernel-buffer interaction rather than a VMAX problem.
2. **Gain response.** The measured gain response is a cliff near
   `0xff`, not a ramp. Should the AE treat gain as a small set of
   discrete steps (and calibrate the real response), rather than
   stepping it by `0x18`?
3. **Bright-pixel metric.** `l_bright_mean` degenerates to the plain
   mean whenever the ROI's max is below ~40 levels, because
   `BRIGHT_MARGIN` is an absolute 40. Would a **relative** margin (e.g.
   "pixels within 40 % of the ROI max", or a percentile) be more robust
   in dim scenes? Or is a percentile/median-based measure better than a
   bright mean altogether?
4. **AE/AF coupling.** Is freezing the AE during a sweep the right
   model, or should the AF instead *re-measure* under the new exposure
   (i.e. let the sweep continue but invalidate only the scores from
   before the exposure change)?
5. **Decoder.** Would porting ZXing's `HybridBinarizer` (local 8×8
   blocks, 5×5 neighbourhood) into the native module be a better use of
   effort than further AE tuning? The `k_quirc` fork already contains
   an unused per-quadrant bilinear threshold path.
6. **Resolution.** Is decoding dense codes from the full 1640×1232
   source worth the complexity, given that the source is already
   available in `camd` and a `H` command existed before?

---

## 6. Incident note (for completeness)

During this investigation a test script sent a `V` (VMAX) command to
`camd` over the frame socket while the daemon was streaming. The
device rebooted to Android. The script had also been sent to the
**control** socket (`camd-ctl.sock`), which does not implement `V` and
correctly answered `ER` — so the two paths behaved differently, which
is itself worth noting. The device was restored with `fastboot boot`
(no flash) and is healthy.

Lesson recorded: **do not change VMAX (or any geometry register) at
runtime on a live pipeline**; the crash was not reproduced by any
exposure/gain write alone.
