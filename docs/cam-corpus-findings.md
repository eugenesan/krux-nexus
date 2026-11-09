# Camera corpus — findings

> Date: 2026-11-06 to 2026-11-08. The first capture sessions: the
> user's paper QR, and the tablet-driven corpus.
>
> **The headline: the frame really was cropped — by the sensor's own
> configuration, not by the daemon and not by the optics.** The binned
> mode set the output size to 1640×1232 but never switched the sensor's
> 2×2 binning on, so the sensor read only the top-left quarter of its
> 3280×2464 window. Setting `x_odd_inc`/`y_odd_inc` to 3 (the value the
> sibling Sony drivers use for binning) restores the full field of
> view; the whole tablet now sits centred in the frame, exactly as the
> Android camera shows it.
>
> **The previous model's "the phone was 3.5× further away" conclusion
> was wrong.** It measured the visible sliver of the tablet in a
> cropped frame and divided by the tablet's full width, which turns a
> 2× crop into an apparent 3.5× distance change. The devices were
> affixed; the scene did not move.
>
> The illumination gradient and the screen moiré are still real
> obstacles, but they come *after* the crop: they could not be judged
> properly while the camera was only seeing a quarter of the scene.

---

## 0. The decode blocker was the focus, not the moiré

The user ran the real Krux page (Tools → Datum → From Camera) and it
read the tablet's code. The offline harness said the same frames did
not decode. The harness was not wrong about the pixels; it was feeding
the decoder frames taken at a **bad or cached focus**.

The QR page runs an autofocus sweep, and the sweep settles at **focus
step 12**, where the sharpness is about **1.77 M**. The frames the
offline tests used were at focus steps 7–32 with sharpness about
0.5 M. At step 12 the code is crisp and decodes; at the others the
moiré and the blur win.

The proof is direct: after a full refocus, the saved **720×960
preview** decodes with the same `tools/quirc-replay` the offline tests
use:

```
$ tools/quirc-replay af_found2.raw 720 960
  decoded=1 time_ms=4.3
  payload=KRUXCAM-V20-DENSE-0003
```

So:

- **The moiré is survivable** at the right focus. It is not, by
  itself, the blocker the earlier experiments suggested.
- **Full resolution is not needed** for the 8 cm v20 code at 21 cm;
  the binned 720×960 preview decodes.
- **The crop fix (binning) is what made this possible**, because it
  restored the field of view so the code is actually in the frame.

The practical rule for the offline harness: a captured frame is only
meaningful if the autofocus has settled first. Capture after a full
refocus, and record the focus step and sharpness with the frame.

---

## 1. The field of view WAS cropped — by the sensor mode

The user observed that in the Android camera the tablet was centred and
wholly visible, while in our preview only about a quarter of the scene
was there and only the top-left quarter of the QR. That was a real
crop, and the user's suspicion was right: the sensor was configured
incorrectly.

### The bug

`camd`'s binned mode programs the sensor with `imx179_settings_9` (the
full 3280×2464 mode) and then `imx179_settings_7` (the "config change"
that sets the output size to 1640×1232 and VMAX to 1252). But
`settings_7` does **not** touch the readout-increment registers:

```c
/* imx179_settings_9, the full-resolution mode */
{ 0x0383, 0x01, 0 },   /* x_odd_inc */
{ 0x0387, 0x01, 0 },   /* y_odd_inc */
```

`0x0383`/`0x0387` are `x_odd_inc`/`y_odd_inc`. `1` means "take every
pixel" (full resolution); `3` is the 2×2-binning value the sibling Sony
IMX074 and IMX135 drivers use for their binned modes
(`binning_factor = 2`). Because `settings_7` leaves them at `1`, the
sensor keeps its full 3280×2464 readout window but is told to output
only 1640×1232 pixels — so it reads the **top-left quarter** of the
sensor and throws the rest away.

That is exactly what the frames showed: the tablet's centre, which is
the centre of the full sensor, landed at the **bottom-right** of the
captured frame, and the QR came out at the **full-resolution** scale
(≈8 px per module, matching the Android photo's 7.7) rather than the
≈4 px a genuinely binned frame gives.

### Why the previous model concluded "3.5× further away"

It measured the tablet's **visible sliver** in the cropped frame
(334 px) and divided by the tablet's **full** width (172.2 mm), giving
1.94 px/mm. Compared with the Android photo's 10.25 px/mm, that looks
like a 5.3× scale difference; after allowing for the 1.49× resolution
ratio it looks like 3.5× more distance. But the 334 px was not the
tablet — it was the part of the tablet that happened to fall inside a
2× crop. A crop and a distance change are indistinguishable if you
measure the wrong object, and the devices were affixed the whole time.

### The fix

Set the readout increments to the binning value in the binned mode:

```c
static const struct imx179_reg binning[2] = {
    { 0x0383, 0x03, 0 },   /* x_odd_inc: 2x2 binning */
    { 0x0387, 0x03, 0 },   /* y_odd_inc: 2x2 binning */
};
sensor_write_regs(binning, 2);
```

**Verified on the device.** With the fix, the source frame and the
720×960 preview show the **whole tablet, centred, with the QR fully in
frame** — the same framing as the Android camera. Before the fix the
tablet sat at the right edge with only a sliver visible.

### What this changes

The crop was masking everything else. The code is now at roughly
**3 px per module** in the 1640×1232 binned frame (the Android photo
gets 7.7 px/module because it is the full 8 MP sensor). The decoder
wants about 5 px/module, so the code still does not decode at this
distance — but now that is an honest resolution question, not a
framing one. The options are: bring the phone closer, show the code
larger on the tablet, or use the full-resolution source (the plan's
Step 6, a crop of the 3280×2464 frame).

### The control that makes the comparison meaningful

**The Android photo of your code decodes with our decoder**, cropped to
the code:

```
$ tools/decode-qr /tmp/opencode/android_ref.jpg 2448 3264
  raw                          809x815  KRUXCAM-V20-DENSE-0003
  bilinear                    809x815  KRUXCAM-V20-DENSE-0003
```

And it still decodes when the code is scaled down to what our **full-
resolution** source would give at the same distance — 541×545 px,
**5.1 px per module**:

```
  at 0.67x (541x545) -> KRUXCAM-V20-DENSE-0003
```

So the decoder, the tablet and the code are all fine. What was wrong
was the sensor mode.

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

**The code's finder patterns were detected** in the frames at
`g0x00ed` and exposures `0x0480`–`0x0780`, at a module pitch of
**8.14 px**. That looked like the Android camera's 7.7 px/module, but
it was not a sign of equal resolution: it was the *full-resolution
scale of a cropped frame*. Because binning was off, the sensor was
reading its full 3280×2464 window one pixel per output pixel and
keeping only the top-left 1640×1232 — so the code appeared at twice
the pitch it should have, and only a sliver of it was in frame.

**After the binning fix** the code is fully in frame at about
**3.9 px/module** (half the Android photo's 7.7, which is exactly what
halving the sensor's resolution does). The decoder can read the code
down to 3.85 px/module when the image is clean — verified by scaling
the Android photo's crop down to 404×404 — so the remaining problem is
the moiré and the low contrast, not the framing.

### What the corpus needs next

The phone now sees the whole code. To decode it reliably at this
distance the image needs either more resolution (the full-resolution
source, Step 6), a larger code on the tablet, or a closer phone.

