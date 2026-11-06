# Camera corpus — findings from the first captures

> Date: 2026-11-06. The first real capture session: the user's paper QR
> (version 17, 70×70 mm, 30 cm) and the setup for the tablet-driven
> corpus.
>
> **The headline: the reason a dense code does not decode is not the
> exposure, not the focus, and not the resolution. It is a 2-pixel
> checkerboard in the sensor's output.**

---

## 1. What was captured

The user's own "HD QR" — **70×70 mm printed on paper, 30 cm from the
phone**, held steady. Captured at a range of exposures through
camd's control socket (`tools/capture-corpus`, `tools/capture-one.py`).

### The code itself

| | |
|---|---|
| Version | **17** (85×85 modules) — measured, not assumed |
| Size | 70×70 mm on paper |
| Distance | 30 cm |
| Size in the source frame | ~510 px across → **6.0 source px per module** |
| Size in the preview | **2.63 preview px per module** |

The version was measured with the decoder's own grid detector
(`tools/locate-qr` and the `dbg` harness): with the noise removed the
decoder reports `grid_size = 85`, and `(85 − 17) / 4 = 17`.

**So the code is version 17 — comfortably inside the decoder's version
27 ceiling.** The `K_QUIRC_MAX_BITMAP` bug does not affect this code.
(It still affects versions 28–40, which is a separate, real bug.)

### The exposure makes a large difference

| exposure | gain | frame mean | code contrast (paper − modules) |
|---|---|---|---|
| 0x0180 | 0x00a5 | 15 | 7 levels |
| 0x0400 | 0x00a5 | 21 | 7 levels |
| **0x04e0** | **0x00ed** | **100** | **139 levels** |

At the exposure the sensor's AE settled on (`0x09d6`, gain `0xff`), the
picture was **dark and flat** — 7 levels of contrast between the paper
and the black modules. At `0x04e0` with gain `0x00ed` the same code
has **139 levels** of contrast and is plainly visible.

This is the AE problem in its clearest form: the AE was walking the
exposure toward a ceiling the sensor silently discards, so it never
reached an exposure that could see the code. (The sensor's own
read-back: `0x0202/0x0203 = 0x04e0` and `0x0340/0x0341 = 0x04e4` — VMAX
is 1252 lines and the legal integration time is 1248. The AE was
commanding 2518.)

---

## 2. The real blocker: a 2-pixel checkerboard

Even at 139 levels of contrast, the decoder **fails to find the code's
finder patterns**. The binarised image comes out *entirely black*:

```
binarized: white=0 black=324900 (of 324900)
```

That is not a threshold problem — it is a **spatial** problem. The
sensor's output has a strong 2-pixel-period checkerboard:

```
source 1640x1232   mean=73.8 std=19.4 | 1px mean|d| = 36.01
preview 720x960    mean=87.9 std=14.3 | 1px mean|d| =  6.13
```

and the checkerboard is a **systematic bias**, not noise:

```
source even-vs-odd column mean: +2.08
source even-vs-odd row mean:    +1.97
```

The even columns are systematically brighter than the odd ones, and the
even rows brighter than the odd ones, everywhere in the frame — on
blank paper as well as on the code. In the 2-pixel averages it is
**33 % of the total variance**.

The preview does not have it (0 %) because camd's box downscale happens
to average over 1.71 source pixels per output pixel in each direction,
which is close to 2 and so cancels the pattern — but that downscale is
also what destroys the resolution.

### The fix, and it is cheap

Removing the 2-pixel component — a per-row and per-column even/odd
subtraction — **keeps the full resolution and makes the code
detectable**:

| image | size | result |
|---|---|---|
| raw | 570×570 | `grids=0` — the code is not found |
| **notched** (2-px component removed) | **570×570** | **`grids=1`, 3 capstones found** |
| 2×2 box average | 285×285 | `grids=1`, 3 capstones found |

And the notch does what it should:

```
raw     : std=33.1  1px mean|d|=34.0
notched : std=26.1  1px mean|d|= 4.5
```

The high-frequency content collapses from 34 to 4.5 while the
resolution is untouched. The 2×2 average reaches the same place but
throws away half the resolution, leaving only **3.0 px per module** —
barely more than the preview's 2.63, which is not enough.

### What still fails

With the code found, the decode still ends in `err=4 (ECC failure)`.
So the finder detection was the *first* barrier, and there is at least
one more behind it. The likely candidates, in order:

1. **Residual noise.** After the notch, 1-px `mean|d|` is still 4.5
   levels; with 6 px per module that is a meaningful fraction of a
   module's edge.
2. **Focus.** The lens is fixed-focus and cannot resolve a small code
   closer than ~50 cm; the sharpness score at the time was low.
3. **The exposure is still not right.** 139 levels of contrast is good,
   but the AE would have to *stay* there.

This is exactly the ordering the review predicted, and the order the
measurements support: **binarisation first, resolution second, the AE
third.** But the specific binarisation problem turned out to be
something no thresholding scheme can fix — the input has to be cleaned
first.

---

## 3. What this changes in the plan

### New: the checkerboard must be removed before anything else

This is a **preprocessing** step, and it belongs in `camd`, on the
source luma, before the frame is handed on. It is:
- cheap (one pass over the frame, O(n)),
- lossless in resolution (unlike the downscale it would replace),
- and it removes 33 % of the variance.

It also **changes the arithmetic of the whole camera**: the preview's
1.71× downscale is doing two jobs at once — resizing *and* accidentally
filtering the checkerboard. If the checkerboard is notched out at the
source, the preview can be built by a cleaner downscale and more of the
source's detail survives.

**Open question for the implementation:** where exactly does the
checkerboard come from? Candidates:
- the IMX179's Bayer pattern and the ISP's demosaic (the sensor runs
  in *binned* mode at 1640×1232, and 2×2 binning of a Bayer array is
  the classic source of a 2-px pattern),
- the CPP's processing,
- or the mode's geometry.

The tree has `cam/cam_chromatrix.h` and the CPP firmware; the
question is whether the fix belongs in the sensor mode (a different
mode without the artefact) or in software. **The software notch is
worth doing regardless**, because it is 20 lines and it works.

### Confirmed: the exposure ceiling is a real bug

The AE's `MAX_EXPOSURE` is `0x09d6` (2518) and the sensor's VMAX is
`0x04e4` (1252). Every exposure above `0x04e0` is discarded. The
capture above shows what that costs: at the AE's chosen exposure the
code has 7 levels of contrast; at the sensor's real maximum it has 139.
**That is the single biggest contrast improvement available**, and it
is a two-line fix.

### Confirmed: the version ceiling is a real bug (but not for this code)

`K_QUIRC_MAX_BITMAP 1954` refuses versions above 27. This code is
version 17, so it is unaffected — but versions 28–40 cannot decode at
all. One-line fix, verified on the host.

### Refined: the 1280-pixel limit shapes the full-resolution design

The decoder refuses any image over 1280 px in either dimension, and
the source frame is 1640×1232. The full-resolution path must
therefore **crop and decode**, not decode the whole frame. (Verified on
the host: a 1100×1100 crop of a source frame decodes; the whole frame
is refused.)

### Revised: local thresholding is *necessary but not sufficient*

The threshold modes (global Otsu, the unused bilinear path, the unused
adaptive path) all failed on this code — because they all threshold a
picture whose high-frequency content is a checkerboard. **They must be
re-tested after the notch**, on cleaned frames. The corpus makes that
possible offline in milliseconds.

---

## 4. The corpus, driven from the tablet

The tablet (Samsung GT-N5110, 800×1280, Android 4.4.2) is now
available over adb and displays a code full-screen in landscape at an
exact pixel size. The chain is verified end to end: `tools/show-qr`
pushes the PNG, the gallery shows it, and **the tablet's own screenshot
decodes back to the expected payload** with the host harness.

```
$ tools/show-qr --target v7-mid --module-px 8
showing: {"payload": "KRUXCAM-V7-MID-0002", "modules": 53, "module_px": 8, ...}

$ # screencap the tablet, rotate to landscape, decode:
decoded=1 time_ms=4.1 | payload=KRUXCAM-V7-MID-0002
```

The display is pixel-exact (the PNG is shown at its natural size), so
the **pixels per module on the screen is known exactly** — which is
what makes the corpus reproducible. The tablet's codes are about
**11×11 cm** physically.

### The corpus to capture

With the tablet in landscape (1280×800) and a code filling most of
the width, at a known number of pixels per module:

| target | version | modules | px/module | note |
|---|---|---|---|---|
| `v2-low` | 2 | 33 | 10 | the easy case |
| `v7-mid` | 7 | 53 | 8 | |
| `v20-dense` | 20 | 105 | 6 | the failing workload |
| `v27-max` | 27 | 133 | 6 | the decoder's current ceiling |

Each at two or three **distances** (which changes the pixels per module
in the camera's view) and across the **exposure ladder** (which
changes the contrast and the noise). That gives the matrix the review
asked for, and every frame of it is replayable offline.

### What the corpus must be captured with

- The phone **held steady**, aimed at the tablet.
- The tablet in **landscape**, filling as much of the frame as
  possible.
- The screen brightness **as high as it goes** — the tablet is a
  light source, and the code's white level is what the AE has to hit.
