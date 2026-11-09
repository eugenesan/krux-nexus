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

Two independent confirmations:

- The kernel reports **no CAMIF geometry errors** at all.
  `msm_vfe32_process_error_status` prints "camif error status"
  whenever the front end's window does not match the sensor's line,
  and it never did.
- camd's own downscale maps the whole source into the preview: the
  preview reconstructed from the source through camd's exact mapping
  correlates at **0.979**, with the four edges mapping to each other
  correctly (`tools/preview-vs-source`).

### So why is the tablet cut off in ours?

**Because the phone is aimed too far to the right.** The tablet's centre
sits at col 483 of our 720-wide preview against col 1180 of Android's
2448-wide frame — and our frame is *rotated* relative to the source
(camd rotates the source 90° counter-clockwise into the portrait
preview), so "left" and "right" in the two are not the same axes.

The measurement that settles it: **the code's finder pattern is at
source col 1480**, and the frame's edge is at 1640, so only **160 px
of the code is in frame**. At the 8 px per module that Android
resolves it at, the code needs **904 px** — we are missing **744 px**
of it. The code is not unreadable; it is *mostly outside the frame*.

**The fix is aiming, not optics:** point the phone a little to the
left of the tablet, or move the tablet further away. The Android
camera gives the reference for what "aimed correctly" looks like.

### The control that makes the comparison meaningful

**The Android photo of your code decodes with our decoder**, cropped
to the code:

```
$ tools/decode-qr /tmp/opencode/android_ref.jpg 2448 3264
  raw                          809x815  KRUXCAM-V20-DENSE-0003
  bilinear                    809x815  KRUXCAM-V20-DENSE-0003
```

So the decoder is fine, the tablet is fine, and the code itself is
fine. The failure is the framing.

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
- **The tablet's screen goes off.** `stay_on_while_plugged_in 3`,
  `screensaver_enabled 0` and a long `screen_off_timeout` keep it up.

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

### The first corpus capture

20 frames of `v20-dense` on the tablet, across the exposure ladder at
two gains. **The exposure writes stuck** (the camera page was closed),
and the daemon's own read-out shows the picture brightening properly:

```
g0x00a5  e0x0180  mean=18     g0x00ed  e0x0180  mean=30
g0x00a5  e0x0280  mean=18     g0x00ed  e0x0280  mean=40
g0x00a5  e0x0380  mean=22     g0x00ed  e0x0380  mean=48
g0x00a5  e0x0480  mean=25     g0x00ed  e0x0480  mean=54
g0x00a5  e0x0580  mean=27     g0x00ed  e0x0580  mean=59
g0x00a5  e0x0680  mean=29     g0x00ed  e0x0680  mean=63
g0x00a5  e0x0780  mean=31     g0x00ed  e0x0780  mean=67
g0x00a5  e0x0880  mean=33     g0x00ed  e0x0880  mean=73
g0x00a5  e0x0980  mean=35     g0x00ed  e0x0980  mean=76
g0x00a5  e0x04e0  mean=26     g0x00ed  e0x04e0  mean=57
```

**The code's finder patterns are now detected** in the frames at
`g0x00ed` and exposures `0x0480`–`0x0780`, at a module pitch of
**8.14 px** — the same 8 px per module that the Android camera
resolves it at. So the camera is resolving the code as well as the
Android camera does.

**But the code is cut off at the frame's edge**, so nothing decodes
yet: only 160 px of a 904-px-wide code is in frame.

### What the corpus needs next

The phone has to be aimed so the whole code is in frame. The Android
photo is the reference: the code should sit well inside the frame,
as it does there.

