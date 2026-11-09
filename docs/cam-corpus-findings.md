# Camera corpus — findings

> Date: 2026-11-06 to 2026-11-08. The first capture sessions: the
> user's paper QR, and the tablet-driven corpus.
>
> **The headline: for a QR code on a screen, the blocker is the
> illumination gradient across the frame. It is not the exposure, not
> the focus, and not the resolution.**
>
> **And: the field of view is NOT cropped.** The user's suspicion that
> the pipeline crops was tested directly against the Android camera on
> the same device, in the same setup, and disproved.

---

## 1. The field of view: not cropped

The user observed that in the Android camera the tablet was centred
and wholly visible, while in our preview it was at the right edge and
cut off. That looked like a crop.

**The test:** the phone was rebooted into Android (never flashed), the
stock camera was opened, and two photographs were taken of the same
scene. They agreed with each other to within **1 px** (the screen
spanned 1765 and 1766 px), so the phone was steady and the reference
is sound. The stub was then restored with `fastboot boot`.

The Android photo is **2448×3264** — the IMX179's full 8 MP sensor.
Ours is **1640×1232** — the binned mode.

The measurement is the same physical object in both: the tablet's
screen, which is 1280 screen px = 172.2 mm wide.

| | the Android photo | our preview |
|---|---|---|
| the frame | 2448×3264 | 720×960 |
| the tablet's screen spans | 1765 px | 473 px |
| **as a fraction of the frame's width** | **72.1 %** | **65.7 %** |

**Our field of view is 1.10× *wider* than the Android camera's**, not
half of it. If the pipeline were taking half of every line — a 2×
crop — the tablet would fill **144 %** of our frame. It fills 66 %.

So the tablet is cut off at our frame's right edge because **the phone
is aimed a little to the left of it**: the tablet's centre sits at col
483 of our 720-wide preview, and at col 1180 of Android's 2448-wide
frame. That is an aiming difference, not an optical one.

The code agrees with the arithmetic. On the tablet the code is 105
modules wide and the Android camera resolves it at **8 px per module**
(the finder pattern's run length), while ours resolves it at fewer —
because our frame is a downscaled 1640×1232, and the preview is a
further downscale of that.

This is consistent with the kernel reporting **no CAMIF geometry
errors** at all (`msm_vfe32_process_error_status` prints
"camif error status" whenever the front end's window does not match
the sensor's line, and it never did).

### The geometry is consistent throughout

- The sensor's HMAX register reads back as `0x0d70` = 3440, the
  *full* mode's line length. The binned mode's override
  (`imx179_settings_7`) sets VMAX to `0x04e4` = 1252 and leaves HMAX
  alone. HMAX is the line length in sensor clocks; the sensor bins
  internally and outputs 1640 pixels. The CAMIF window
  (`pixels_per_line = 1640`, `last_pixel = 1639`) matches the output,
  so it takes the whole line.
- camd's own downscale maps the whole source into the preview: the
  preview reconstructed from the source through camd's exact mapping
  correlates at **0.979**, and the four edges map to each other
  correctly (`tools/preview-vs-source`). **Nothing is cropped in the
  daemon either.**

---

## 2. The paper QR (version 17, 70×70 mm, 30 cm)

### What it is

Measured, not assumed — the decoder's own grid detector reported
`grid_size = 85`, and `(85 − 17) / 4 = 17`:

| | |
|---|---|
| Version | **17** (85×85 modules) |
| Size in the source frame | ~510 px across → **6.0 source px per module** |
| Size in the preview | **2.63 preview px per module** |

Version 17 is inside the decoder's version-27 ceiling, so the
`K_QUIRC_MAX_BITMAP` bug does not affect it.

### The exposure makes a large difference

| exposure | gain | frame mean | code contrast |
|---|---|---|---|
| 0x0180 | 0x00a5 | 15 | 7 levels |
| 0x0400 | 0x00a5 | 21 | 7 levels |
| **0x04e0** | **0x00ed** | **100** | **139 levels** |

At the exposure the AE settled on (`0x09d6`, gain `0xff`), the code
had **7 levels** of contrast. At the sensor's real maximum it has
**139**. The AE was walking toward a ceiling the sensor silently
discards.

### The sensor's 2-pixel artefact

The output carries a systematic 2-pixel-period pattern:

```
source even-vs-odd column mean: +2.08
source even-vs-odd row mean:    +1.97
```

**33 % of the total variance** on blank paper. Removing it keeps the
full resolution and makes the code detectable:

| image | result |
|---|---|
| raw | `grids=0` — the code is not found |
| **notched** (full resolution) | **`grids=1`, 3 capstones** |
| 2×2 box average | `grids=1`, 3 capstones — but at half the resolution |

On the tablet the pattern is **diagonal** rather than axis-aligned,
and there the diagonal component is **61 levels** against the code's
own 46 — the artefact is bigger than the signal.

---

## 3. What actually breaks the decode: the gradient and the moiré

Each effect was added to a faithful simulation of the camera's view
(the tablet's own screenshot, magnified into the 1640×1232 source
frame, then camd's exact rotation and box downscale):

```
neither                                             OK
gradient                                            OK
moire 61                                            OK
sensor 2-px                                         OK

gradient + moire                                    -     <-- breaks it
moire + sensor                                      OK
gradient + sensor                                   OK

all three                                           -
```

**Only the gradient and the moiré together break it.** The gradient
scales the code's *local* contrast down in the dark part of the frame
while the moiré stays at its full 61 levels there, so the artefact is
larger than the signal and no threshold — global, bilinear or
adaptive — has anything left to work with.

How strong the gradient has to be, with the moiré present:

| the bright side | the dark side | the range | |
|---|---|---|---|
| 55 | 20 | 2.8× | fails |
| 55 | 35 | 1.6× | fails |
| 58 | 45 | 1.3× | fails |
| 60 | 50 | 1.2× | fails |
| **62** | **55** | **1.1×** | **decodes** |

The measured case was **5.8×**.

### What does not help

- **A bigger code on the screen.** Tested to 12.2 source px per
  module; no effect while the gradient is there.
- **A different threshold.** All three modes fail identically; the
  problem is upstream of the threshold.
- **The camera's exposure.** The whole ladder from `0x0180` to
  `0x04e0` fails the same way.

### What does help

- **Even lighting.** A dim but even code decodes; a bright code with
  a 5.8× gradient does not. This is the one thing the user controls.
- **A matte screen protector or tracing paper** over the tablet: evens
  the illumination and knocks the moiré down at once.

---

## 3a. The field of view: not cropped

This was worth settling early, because if the frame were cropped the
whole resolution argument would change.

**The test.** The phone was rebooted into Android (never flashed), the
stock camera opened, and two photographs taken of the same scene. They
agreed within **1 px**, so the phone was steady. The stub was then
restored with `fastboot boot`. The measurement is the same physical
object in both: the tablet's screen, 1280 screen px = 172.2 mm wide.

| | the Android photo | our preview |
|---|---|---|
| the frame | 2448×3264 (the full 8 MP sensor) | 720×960 (from the 1640×1232 source) |
| the tablet's screen spans | 1765 px | 473 px |
| **as a fraction of the frame's width** | **72.1 %** | **65.7 %** |

**Our field of view is 1.10× *wider* than the Android camera's.** A 2×
crop would put the tablet at 144 % of our frame; it is at 66 %. The
tablet is cut off at our right edge because the phone is aimed a
little to the left of it — its centre sits at col 483 of our 720-wide
preview against col 1180 of Android's 2448-wide frame. That is aiming,
not optics.

Two independent confirmations:

- The kernel reports **no CAMIF geometry errors** at all.
  `msm_vfe32_process_error_status` prints "camif error status"
  whenever the front end's window does not match the sensor's line,
  and it never did.
- camd's own downscale maps the whole source into the preview: the
  preview reconstructed from the source through camd's exact mapping
  correlates at **0.979**, with the four edges mapping to each other
  correctly (`tools/preview-vs-source`).

So the resolution argument stands: the source is 1640×1232 and the
preview is 720×960, and the full-resolution path is worth restoring.
The Android camera's own 2448×3264 photo of the same scene is a
useful third data point — it resolves the code at **8 px per module**
where our preview gives it about 2.6.

---

## 4. Other findings

- **The AE's exposure ceiling is a real bug.** `MAX_EXPOSURE` is
  `0x09d6` = 2518 lines, but the sensor's VMAX is `0x04e4` = 1252, so
  every exposure above `0x04e0` is discarded without the controller
  knowing. The device's own AE log shows the controller pushing against
  that ceiling for five consecutive steps with the measured level
  frozen.
- **The decoder's version ceiling is a real bug** (but not for this
  code). `K_QUIRC_MAX_BITMAP 1954` refuses versions above 27;
  raising it to 3917 makes v28–v30 decode immediately.
- **The camera page must be closed before a capture.** While it is
  open, the firmware's auto-exposure writes the exposure every third
  frame and fights the capture script. `tools/send-key ENTER` closes
  it (the hammerhead's ENTER key is KEY_POWER on `/dev/input/event0`).
- **The stub ramdisk must be gzip.** The stock bootloader looks for a
  gzip ramdisk and does not look for xz; an xz-packed ramdisk boots
  nothing and the device comes straight back to Android with no error
  anywhere.

---

## 5. The corpus, driven from the tablet

The tablet (Samsung GT-N5110, 800×1280, Android 4.4.2) shows the code
at an exact, **verified** size: `tools/show-qr` draws the PNG at the
screen's size with the code at the requested pixels per module, then
measures the pitch back from the tablet's own screenshot and reports
`"verified": true`.

The codes are about **9×9 cm to 11×11 cm** physically on the tablet.

| target | version | modules | px/module |
|---|---|---|---|
| `v2-low` | 2 | 33 | 10 |
| `v7-mid` | 7 | 53 | 8 |
| `v20-dense` | 20 | 105 | 6 |
| `v27-max` | 27 | 133 | 6 |
