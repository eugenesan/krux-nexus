# Camera AF/AE — the plan, after the review

> Companion to `docs/cam-brief` (`cam-ae-af-brief.md`) and the review
> (`docs/cam-bringup-af_ae_qr-review.md`).
>
> Date: 2026-11-06. This is the order I intend to work in, and why.

---

## 0. Where the review and I agree, and where I would adjust it

The review is good. It reached the same three conclusions I did from
the measurements — that the exposure ceiling is unreachable, that the
gain response is not the ramp the AE assumes, and that the bright-pixel
metric collapses in dim scenes — and it added two things I had not
thought through:

- **The gain response may not be the sensor's fault at all.** It asks
  whether the cliff is the IMX179's gain encoding, a register-width
  problem, an invalid lower range, or **a measurement artifact caused
  by a nearly black target**. That last one is the honest possibility:
  my sweep was taken with the camera on a dark desk, and the review is
  right to make "redo this on a lit target" a precondition rather than
  a conclusion.
- **The decoder's thresholding is probably the largest single
  limitation**, ahead of resolution and ahead of the AE. I had listed
  it as an equal contender; on reflection the review has the better of
  the argument, because a global Otsu threshold is a *guaranteed* miss
  for a small bright code in a large dark frame, whereas the AE
  problems are conditional on the scene.

### Where I would adjust the review's order

The review's experiment order (its §"Required experiment order") is
sound, and I intend to follow it. Two adjustments:

**1. The exposure ceiling should be fixed *first*, not third.** The
review puts "correct exposure ceiling only" as experiment 2, after a
baseline. I would rather do it as experiment 1, before the baseline
sweep, because **the baseline itself is measured through a broken
controller**. Every frame the AE captures today is captured at an
exposure the sensor silently discarded; the baseline numbers would
describe a controller that is not the one we are going to ship. Fixing
the ceiling first makes the baseline *meaningful*. The ceiling fix is
two lines and provably safe (it only ever lowers a value the sensor
was already discarding), so it carries no risk of polluting the
measurement.

**2. The review's Phase 0 (benchmark) is too heavy for its own good.**
It asks for a QR corpus with versions, ECC levels, payload lengths,
physical sizes, viewing distances, pixels-per-module, 20+ attempts
per configuration, median and p90 latency, saved artifacts, and a
failure classification for every frame. That is a research-grade
harness, and building it *before* any code change would consume most
of the remaining effort on instrumentation.

The good news is that **the two most valuable parts of Phase 0 are
already available**, and I verified both today:

- **The decoder already builds and runs on the host.** `k_quirc` is
  self-contained (four `.c` files, no external dependency beyond
  `libm`), so it compiles into a 43 KB host binary in seconds:

  ```
  gcc -O2 -std=gnu99 -o qtest main.c k_quirc.c k_quirc_identify.c \
      k_quirc_decode.c k_quirc_version.c -lm
  ```

  This is the single most important fact for the plan: **the whole
  decoder-side experiment matrix can be run offline, on the host, in
  milliseconds, with no device, no cold boot, and no risk.** The
  review's Phase 4.2 offline replay harness is not a nice-to-have; it
  is the thing that makes everything else cheap.

- **The local-threshold paths already compile.** Both
  `K_QUIRC_BILINEAR_THRESHOLD` and `K_QUIRC_ADAPTIVE_THRESHOLD` build
  cleanly as host binaries (47 KB each) with no code change — they are
  simply not enabled in the device build. So the "existing bilinear
  path" experiment needs no implementation work at all.

- **The full-resolution source luma is already on disk.** `camd` writes
  the undownscaled source to `/data/krux/scan_full.raw` when asked to
  dump. I pulled it: 2,020,480 bytes = 1640×1232 exactly.

So I propose a **trimmed Phase 0**: one fixed test corpus (a handful
of QR codes at known versions and sizes, displayed on the phone screen
and printed), captured **once** as a set of saved luma frames (both
resolutions), plus the host harness to replay them. Everything after
that is a host-side experiment. The device is only needed to *capture*
frames, not to *evaluate* changes.

---

## 1. Verified facts this plan is built on

Measured or read directly from the tree and device today:

| Fact | How it was established |
|---|---|
| SoC is **MSM8974**, not MSM8939 | `hammerhead_defconfig: CONFIG_ARCH_MSM8974=y`; device `/proc/cpuinfo` |
| Sensor VMAX is `0x04e4` = **1252 lines** | camd register read-back: `0x0340=0x0004 0x0341=0x00e4` |
| Sensor's own exposure is `0x04e0` = **1248 lines** = VMAX−4 | read-back: `0x0202=0x0004 0x0203=0x00e0` |
| AE's `MAX_EXPOSURE` is `0x09d6` = **2518 lines** = 2.01× VMAX | `camera.py:46` |
| The AE's ceiling is never raised in QR mode | `_auto_expose()` returns before `set_vts(DEFAULT_VTS)` for non-entropy modes |
| The sensor's **real** gain registers (`0x0208`/`0x020a`) are never touched | grep over the tree: no hits |
| `k_quirc` builds standalone on the host | `gcc ... -lm` → 43 KB binary |
| `K_QUIRC_BILINEAR_THRESHOLD` builds on the host | `gcc -DK_QUIRC_BILINEAR_THRESHOLD` → 47 KB binary |
| `K_QUIRC_ADAPTIVE_THRESHOLD` builds on the host | `gcc -DK_QUIRC_ADAPTIVE_THRESHOLD` → 47 KB binary |
| The full-res source luma is already dumped | `/data/krux/scan_full.raw` = 2,020,480 bytes |
| The source carries ~2× the gradient energy of the preview | measured: source gx=0.86 gy=0.86, preview gx=0.50 gy=0.38 |
| The firmware's MicroPython heap is **32 MB** | `init-stub.c:347`: `execl(binary, "micropython", "-X", "heapsize=32M", ...)` |

The last two matter more than they look. The 32 MB heap means the
full-resolution path's memory cost is **not** a constraint: the luma
plane is 2.0 MB and the RGB565 buffer the image module builds
alongside it is 4.0 MB, against a 33.5 MB heap. And the gradient-energy
measurement confirms the preview genuinely destroys detail rather than
merely resampling it — the source has ~1.7–2.3× the edge energy per
pixel of the downscaled preview.

---

## 2. The plan

### Step 1 — Host harness and a saved corpus (the enabler)

**Status: the harness is built** (`tools/quirc-replay`, commit
`0a9e2d5`). It reads a saved raw luma frame, runs it through a chosen
threshold mode, and reports the decode result and the time it took:

```
tools/quirc-replay <file.raw> <w> <h> [--threshold global|bilinear|adaptive]
```

`k_quirc` is self-contained, so this is a 43 KB host binary built in
seconds. **The harness was verified against the ground truth**: it
decodes the corpus targets and reports the expected payloads.

**Also built:** `tools/capture-corpus`, which drives camd's control
socket to capture the corpus (preview luma + full-resolution source
luma + a JSON sidecar with the exposure, gain and the daemon's
read-out for each frame).

**What is left for this step is the capture itself** — the one part
that needs the device and the user. The targets are prepared; the
procedure is in §3 below.

#### Two limits found while making the ground truth

Both were found by decoding the prepared targets with the host
harness, and both change the plan:

**1. The decoder's version ceiling is version 27, not 40.** The
`k_quirc` fork documents support "up to version 40", and the version
database has all 40 entries — but `k_quirc.h` has:

```c
#define K_QUIRC_MAX_BITMAP 1954  /* ceil(125*125/8) for version 27 */
```

`k_quirc_decode.c` checks the reserved-bitmap size against that
constant and refuses anything larger. Version 27 needs 1954 bytes
exactly; version 28 needs 2081 and is rejected with
`K_QUIRC_ERROR_INVALID_GRID_SIZE`.

Raising the constant to 3917 (version 40's 3917 bytes) makes v28, v30
decode immediately — verified on the host, one line, no other change.
**This is a real bug and it is very likely part of why the "HD QR"
never decoded**: a code denser than version 27 cannot decode at all,
at any exposure, with any thresholding, at any resolution. It is worth
checking what version the failing code actually is.

**2. The decoder refuses any image larger than 1280 pixels in either
dimension.** `K_QUIRC_MAX_IMAGE_DIM` is 1280, checked in
`k_quirc_decode_grayscale`. The **full-resolution source frame is
1640×1232 — it is rejected outright.** The preview (720×960) is
accepted.

This is decisive for Step 6: the full-resolution path cannot work as
a straight "decode the whole source frame", because the decoder will
not accept it. It must be one of:

- raise `K_QUIRC_MAX_IMAGE_DIM` (the decoder's working memory scales
  with the image, so this needs a look at the heap and the frame rate),
- or **decode a crop** of the source frame corresponding to the QR's
  position in the preview — which the review already suggests, and
  which is now *necessary* rather than merely an optimisation. A
  1100×1100 crop of the source decoded perfectly on the host while the
  whole frame failed.

So Step 6 becomes "crop-and-decode" rather than "decode the whole
frame", unless the dimension limit is raised as well.

#### 3. The field of view is NOT cropped (settled 2026-11-08)

The tablet was cut off at the edge of our preview while the Android
camera showed it whole and centred, which looked like a crop. It was
tested directly and it is not one.

The phone was rebooted into Android (never flashed), the stock camera
opened, and two photographs taken of the same scene — they agreed
within **1 px**, so the phone was steady. The stub was restored with
`fastboot boot`. The measurement is the same physical object in both:
the tablet's screen, 172.2 mm wide.

| | the Android photo | our preview |
|---|---|---|
| the frame | 2448×3264 (the full 8 MP sensor) | 720×960 (from the 1640×1232 source) |
| the tablet's screen spans | 1765 px | 473 px |
| **as a fraction of the frame's width** | **72.1 %** | **65.7 %** |

**Our field of view is 1.10× *wider* than the Android camera's.** A 2×
crop would put the tablet at 144 % of our frame; it is at 66 %. The
tablet is cut off because the phone is aimed a little to the left of
it — its centre is at col 483 of our 720-wide preview against col 1180
of Android's 2448-wide frame.

Two independent confirmations: the kernel reports **no CAMIF geometry
errors** at all (it prints "camif error status" whenever the front
end's window does not match the sensor's line), and camd's own
downscale reconstructs the preview from the source with a correlation
of **0.979**, the four edges mapping to each other correctly.

**So the resolution argument is unchanged, and the Android camera's
own photo is a useful third data point: it resolves the code at 8 px
per module where our preview gives it about 2.6.**

### Step 2 — Exposure ceiling (one line, provably safe)

Set `MAX_EXPOSURE` to the sensor's real limit, derived rather than
duplicated: read VMAX from the sensor at start-up (camd already
dumps the geometry registers) and use `VMAX - 4`.

In the firmware that means the Python side needs to know the limit.
Two options:

- (a) camd exposes the limit: the `E` socket command already knows
  `cam_exposure`; have camd **clamp** the exposure it writes and
  report the value it actually applied. Then the firmware cannot
  command an impossible exposure at all, and the two sides cannot
  disagree. **This is the better option** — the clamp belongs where
  the register write happens, and it makes the bug structurally
  impossible rather than merely corrected.
- (b) the firmware asks camd for the limit once at start-up.

I favour (a) with a small addition: camd clamps to `VMAX - 4` and
**echoes the applied value back**, and the firmware adopts the echoed
value into `_ae_exposure`. Then the AE's internal state can never
disagree with the sensor.

**Also in this step:** anti-windup. When the exposure is at its limit
and the picture is still too dark, the controller must stop and report
`ae_limited_dark` rather than continuing to command an impossible
value.

**Done when:** the AE log shows the exposure never exceeding
`0x04e0`, and a scan at a dark scene reports being limited rather than
sitting at a fake ceiling.

### Step 3 — Decoder thresholding (host-only, the big one)

With the harness from Step 1, this is pure host work:

1. Decode the saved preview frames with global Otsu (the baseline).
2. Decode them with the bilinear/per-quadrant path (`-DK_QUIRC_BILINEAR_THRESHOLD`).
3. Decode them with the adaptive-offset path (`-DK_QUIRC_ADAPTIVE_THRESHOLD`).
4. Decode the **full-resolution** frames the same way.
5. If local thresholding is not enough, implement a ZXing-style
   `HybridBinarizer` (8×8 blocks, 5×5 neighbourhood) in the native
   module and test it host-side too.

**This is where the answer to the review's central question comes
from**, and it costs no device time at all.

**But the measurements say the thresholding is not the whole story.**
The simulation in `docs/cam-corpus-findings.md` separates the effects,
and the result is:

```
neither                                             OK
gradient                                            OK
moire 61                                            OK
sensor 2-px                                         OK

gradient + moire                                    -     <-- breaks it
```

**Only the gradient and the moiré together break the decode.** So
Step 3 must test each effect separately, not just the threshold mode:
the thresholding has to be tried on a frame that has had the 2-pixel
artefact removed and the illumination flattened, or the result will
be the same failure every time and will tell us nothing.

The order within Step 3 therefore becomes:

3a. **Remove the 2-pixel artefact** (a per-row and per-column even/odd
    subtraction, plus the diagonal variant) and re-test the threshold
    modes.
3b. **Flatten the illumination** and re-test.
3c. Only then, if it still fails, implement the `HybridBinarizer`.

**Done when:** we have a table of success rate per (resolution ×
threshold mode × artefact treatment) over the corpus, and the winning
combination is identified.

### Step 4 — Gain characterisation (device, lit target)

This is the one measurement that genuinely needs the device *and* a
properly lit scene — which is why the review is right that my dark-desk
sweep was inconclusive.

Sweep gain at a fixed, low exposure against a **uniform bright
target**, reading back the register each time, and record the response
curve. Then decide whether the controller should use a calibrated
lookup table of gain states rather than adding `0x18` blindly.

**Worth checking in the same session:** whether `0x0204/0x0205` is
really the analog gain, or whether the IMX179's *real* gain registers
(`0x0208`/`0x020a`) are the ones that matter and are simply never
touched today. The tree's own documentation and the register read-back
disagree about what `0x0204/0x0205` is, and the review's Phase 2
question ("is this the actual IMX179 analog-gain encoding, a
register-width or write-format issue, an invalid lower range, or a
measurement artifact") is exactly the right question.

**Done when:** we know the shape of the gain response, and whether the
"gain" we have been writing is gain at all.

### Step 5 — AE measurement: percentiles

Replace the bright-pixel mean with a percentile-based measure computed
from a 256-bin histogram of the ROI (P50, P90, P95, P99, clipped
fraction). The native image module is the right place for it: the
histogram is one pass over the ROI, and the Python side just reads the
numbers.

Start from a **P95 target of 200–225** with a **clipped-high limit of
1–3 %**, and tune from the corpus. The reason to move away from the
bright-pixel mean is the review's: with an absolute 40-level margin
the metric degenerates to the plain mean in dim scenes and is
destabilised by a few hot pixels.

The percentile measurement also gives the review's diagnostic
classification for free: a frame that fails to decode can be
classified offline as NOISE_LIMITED / EXPOSURE_CLIPPED / etc. from
its histogram and its focus score, which is much more useful than
guessing.

**Done when:** the AE converges to the P95 target without commanding an
impossible exposure, and the failure classification works on the
corpus.

### Step 6 — Full-resolution fallback (device)

Only if Step 3 shows that resolution actually helps.

The removed code is recoverable: `sensor.snapshot_full()` and camd's
`H` command are in the tree's history (`16fa1cd`/`2d69a789` added,
`3b5761d`/`82f07bfc` removed). The removal was collateral damage from
a preview-stability change, not a judgement on the approach.

The memory cost is fine (2 MB luma against a 32 MB heap). But note the
limit found in Step 1: **the decoder refuses the 1640×1232 frame
outright**, so this step is a **crop-and-decode**, not a whole-frame
decode:

- find the code's position in the preview (the decoder already
  reports the corner positions even when the decode fails, so a
  located-but-undecoded code is measurable),
- crop the corresponding region out of the source luma, with a quiet
  margin,
- decode the crop, which is under the 1280-pixel limit.

A 1100×1100 crop of a synthetic source frame decoded on the host
while the whole frame failed, so the approach is sound.

**Done when:** dense codes decode reliably with the preview path
untouched, and the full-res attempt costs an acceptable latency.

### Step 7 — AF scheduling (device, and last among the decoder-side changes)

Only after Steps 2–6, because until then the AF may not be the
limiter at all. The review's ordering here is right.

- Shorten the "unread" trigger from 15 frames to 3–5, **but only
  after** the full-resolution and local-threshold attempts — the
  review's decision order is exactly right: decode failure should
  first be addressed as binarisation/sampling, and only then as focus.
- Multi-frame scoring with a median, to reject PWM-banded frames.
- Score normalisation above a noise floor, if the data shows the
  current metric rewards noise.

### Step 8 — Entropy capture (the original next task)

The entropy capture is measured on the delivered picture, so it
depends on the AE being correct. It should come **after** Steps 2 and
5, and it must keep its own rules: the digital luma gain stays off,
the manual exposure lock is ignored, and the unsharp mask stays off.

---

## 3. Order of work, and what each step depends on

```
Step 1  host harness + corpus          <- harness DONE; corpus needs the user
   |                                      (the tablet is up; the camera
   |                                       page must be CLOSED first)
Step 2  exposure ceiling (camd clamp)   <- 2 lines, provably safe
   |                                      do this FIRST: the baseline
   |                                      is otherwise measured through
   |                                      a controller commanding
   |                                      exposures the sensor discards
Step 3  decoder thresholds (host)       <- no device needed
   |                                      3a: remove the 2-pixel artefact
   |                                      3b: flatten the illumination
   |                                      3c: then the threshold modes
   |  (plus: the version ceiling, found in step 1, is a 1-line fix
   |   and may matter more than anything else here)
Step 4  gain characterisation (device)  <- needs a lit target + user
   |
Step 5  percentile AE (native module)   <- informed by 3 and 4
   |
Step 6  full-res fallback (device)      <- only if step 3 says so;
   |                                      a CROP of the source, not the
   |                                      whole frame (1280-pixel limit)
   |
Step 7  AF scheduling (device)          <- only if 2-6 leave focus as
   |                                      the limit
Step 8  entropy capture                 <- after 2 and 5
```

Steps 3 and 4 are independent of each other and can be interleaved.
Step 2 is independent of everything except that it should precede any
measurement.

### The physical finding that reorders the work

The measurements say the decode fails because of the **illumination
gradient together with the screen's moiré**, and not because of the
thresholding alone:

```
neither                                             OK
gradient                                            OK
moire 61                                            OK
gradient + moire                                    -     <-- breaks it
```

So there are two tracks, and they need different people:

- **The physical track** is the user's: even lighting on the tablet.
  A dim but even code decodes; a bright code with a 5.8× gradient
  does not, and the gradient has to come down to about 1.1×. This is
  worth doing before any of the software work is judged, because the
  software work is measured against the corpus.
- **The software track** is ours: remove the 2-pixel artefact, flatten
  the illumination in the decoder, fix the exposure ceiling, and use a
  local threshold.

**These interact.** A flat-field correction in the decoder is a
software way of doing what even lighting does physically, and it would
work regardless of how the room is lit. It is worth building, because
it makes the camera robust rather than dependent on the user's
lighting.

### The device must be in a known state for a capture

Two things have to be true before a capture means anything:

1. **The camera page in Krux must be closed.** While it is open, the
   firmware's auto-exposure writes the exposure every third frame and
   fights the capture script. `tools/send-key ENTER` closes it.
2. **The exposure write must stick.** Check it in `camd.log`: the
   last line should be `[sensor] E exp=0x.... gain=0x.... (ctl)`, and
   nothing after it.

---

## 4. What I expect the answer to be, and why

For the record, so it can be falsified:

- **Local thresholding will matter** for a screen-displayed code in a
  dark frame, because a global threshold computed over the whole frame
  is dominated by the dark surroundings — but the simulation says it
  is **not sufficient on its own**: the threshold only gets to help
  once the 2-pixel artefact and the gradient are dealt with.
- **Full resolution will matter** for dense codes, because the
  measurement shows the source carries ~1.7–2.3× the edge energy per
  pixel that the preview does — the downscale is not throwing away
  information, it is destroying it. The Android camera resolves the
  same code at 8 px per module where our preview gives it 2.6.
- **The AE fix is necessary but not sufficient.** It removes a real
  bug (the controller commanding an exposure the sensor discards) and
  it makes the measurement honest, but on its own it will not make a
  dense code decode.
- **The AF is probably not the main problem**, as the review says. The
  coarse/fine sweep finds the peak reliably (steps 6–9 at arm's
  length) and the sharpness metric is reasonable for contrast-detect
  AF. What is wrong with it is *scheduling* — it fires when the real
  problem is binarisation.
- **Even lighting will matter more than any of the software**, if the
  user can achieve it. The gradient has to come down from 5.8× to
  about 1.1× for the code to decode at all, and no threshold mode
  substitutes for it. A flat-field correction in the decoder is the
  software equivalent, and is worth building for that reason.

If the corpus contradicts any of that, the corpus wins.

---

## 5. Risks and things I am deliberately not doing

- **Not raising VMAX.** The review is right to forbid it for now: it
  halves the frame rate, it changes frame timing that the CSI/CPP
  state assumes, and a runtime VMAX write on a live pipeline already
  crashed the device once today. If longer integration is genuinely
  needed it becomes a separate sensor mode selected before stream-on.
- **Not touching the entropy capture's rules.** The digital gain stays
  off there, and the entropy picture must not be reshaped.
- **Not rewriting the AF metric** until Steps 2–6 have shown that
  focus is what is left.
- **Not building the review's full Phase-0 harness.** The trimmed
  corpus plus the host replay program covers the same ground for a
  fraction of the effort, because the decoder experiments do not need
  the device at all.
- **Keeping every experiment in its own commit**, per the review, so
  any step can be reverted independently.
