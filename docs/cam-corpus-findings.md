# Camera corpus — findings

> Date: 2026-11-06 to 2026-11-08. The first capture sessions: the
> user's paper QR, and the tablet-driven corpus.
>
> **The headline: for a QR code on a screen, the blocker is the
> illumination gradient across the frame. It is not the exposure, not
> the focus, and not the resolution.**

---

## 1. The paper QR (version 17, 70×70 mm, 30 cm)

### What it is

Measured, not assumed — the decoder's own grid detector reported
`grid_size = 85` with the noise removed, and `(85 − 17) / 4 = 17`:

| | |
|---|---|
| Version | **17** (85×85 modules) |
| Size in the source frame | ~510 px across → **6.0 source px per module** |
| Size in the preview | **2.63 preview px per module** |

Version 17 is comfortably inside the decoder's version-27 ceiling, so
the `K_QUIRC_MAX_BITMAP` bug does not affect it.

### The exposure makes a large difference

| exposure | gain | frame mean | code contrast |
|---|---|---|---|
| 0x0180 | 0x00a5 | 15 | 7 levels |
| 0x0400 | 0x00a5 | 21 | 7 levels |
| **0x04e0** | **0x00ed** | **100** | **139 levels** |

At the exposure the AE settled on (`0x09d6`, gain `0xff`), the code had
**7 levels** of contrast. At the sensor's real maximum it has **139**.
The AE was walking toward a ceiling the sensor silently discards — the
exposure ceiling bug, confirmed, and worth two lines.

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
| raw | `grids=0` — not found |
| **notched** (full resolution) | **`grids=1`, 3 capstones** |
| 2×2 box average | `grids=1`, 3 capstones — but at half the resolution |

So on paper, the notch is the fix, and it costs nothing.

---

## 2. The tablet (a screen, not paper)

### The display chain

The tablet shows the code at an exact, **verified** size: the PNG is
drawn at the screen's size with the code at the requested pixels per
module, and the tool measures the pitch back from the tablet's own
screenshot (`"verified": true`).

### What the camera sees

| | |
|---|---|
| The tablet's own screenshot | **decodes** — the code is fine |
| The camera's view of the same code | does not decode |
| The code on the tablet | 5.54 screen px per module |
| The camera's source frame | 7.43 source px per module |
| The camera's preview | ~3.1 preview px per module |

### The three effects, measured

| effect | size | measured as |
|---|---|---|
| illumination gradient | **15 → 87 levels** (a 5.8× range) | the brightness map across the frame |
| screen moiré | **61 levels**, a 2-pixel diagonal | the even/odd difference along the diagonals |
| sensor artefact | 16 levels, on the axes | the even/odd difference on the rows and columns |

Note the moiré is **larger than the code's own contrast** (61 against
46 levels), and it is *diagonal* here, where on paper it was
axis-aligned.

### The decisive experiment

Each effect was added to a faithful simulation of the camera's view
(the tablet's own screenshot, magnified into the 1640×1232 source
frame, then camd's exact rotation and box downscale into the 720×960
preview):

```
neither                                             OK
gradient                                            OK
moire 61                                            OK
sensor 2px                                          OK

gradient + moire                                    -     <-- breaks it
moire + sensor                                      OK
gradient + sensor                                   OK

all three                                           -
```

**Only the gradient and the moiré together break it.** Each alone is
survivable.

### Why

The gradient scales the code's *local* contrast down in the dark part
of the frame while the moiré stays at its full 61 levels there. So in
the dark region the artefact is larger than the signal, and the
threshold — global, bilinear or adaptive — has nothing left to
threshold. Making the code bigger does not help (tested up to 12.2
source px per module); flattening the illumination does.

### How strong the gradient has to be

With the moiré at its measured 61 levels:

| the bright side | the dark side | the range | |
|---|---|---|---|
| 55 | 20 | 2.8× | fails |
| 55 | 35 | 1.6× | fails |
| 58 | 45 | 1.3× | fails |
| 60 | 50 | 1.2× | fails |
| **62** | **55** | **1.1×** | **decodes** |

The measured case was 5.8×. **The gradient has to come down to about
1.1× — roughly a stop — for the code to decode at all.**

---

## 3. The answer to the lighting question

**Yes, the lighting is the problem, and it is worth fixing.**

It is not the brightness that matters but the *evenness*: the code
has to be lit within about a tenth of a stop across the frame, with
the moiré present. A dim but even code decodes; a bright code with a
5.8× gradient does not.

What helps, in order of effect:

1. **Even lighting on the screen.** A lamp or a sheet of paper
   diffusing the tablet's own light, or moving the phone so the
   gradient falls across the code rather than along it. The gradient
   is what kills it, and it is the one thing the user controls.
2. **A closer phone.** The tablet is a large, bright, even source; the
   gradient comes from the *room*, and the phone's own position
   decides how much of the room's falloff lands on the code.
3. **A matte screen protector or a sheet of tracing paper** over the
   tablet: it evens the illumination and knocks the moiré down at
   once.

What does **not** help, and is worth knowing before spending time on
it:

- **A bigger code on the screen.** Tested to 12.2 source px per
  module; it makes no difference while the gradient is there.
- **A different threshold.** Global Otsu, the unused bilinear path and
  the unused adaptive path all fail identically. The problem is
  upstream of the threshold.
- **The camera's exposure.** The exposure ladder from `0x0180` to
  `0x04e0` and the gain from `0x00a5` to `0x00ff` all fail the same
  way.

---

## 4. What this means for the plan

The ordering the review proposed — binarisation, then resolution, then
the AE — was right for the paper code, and it is **wrong for the
screen code**, which is the case that matters.

For a screen:

1. **The illumination gradient is the first thing to fix**, and it is
   a physical fix, not a software one. It is also the thing the
   review's "percentile AE" would *not* fix: a percentile measured
   over the ROI would follow the gradient, not remove it.
2. **A local threshold is still needed**, and the review's case for it
   stands: the gradient alone (5.8×) is survivable in simulation, but
   only just, and only because the code is bright. The per-quadrant
   bilinear path in `k_quirc` is the cheapest way to get there.
3. **The 2-pixel sensor artefact must be notched out** regardless —
   on paper it was the whole blocker.
4. **The full-resolution path becomes attractive again**: at 7.4 source
   px per module the code has more than twice the preview's, and the
   2-pixel artefact is a fixed pattern that does not get worse with
   resolution.

So the software work is: notch the 2-pixel artefact, use a local
threshold, and correct the exposure ceiling. The physical work is:
even lighting.
