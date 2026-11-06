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

**What:** build a small host program, `tools/quirc-replay`, that reads
a saved raw luma frame, runs it through a chosen threshold mode, and
reports decode success, payload, and timing. Add a `--threshold
global|bilinear|adaptive` switch and a `--invert` flag.

**Also:** capture the corpus. This is the one step that needs the
device and the user. Procedure:

- Fix the test targets: 3 QR codes (version ~2 low density, ~7
  medium, ~20+ dense) at two displayed sizes, on a second screen and
  printed on paper. Record version, size, and distance.
- For each target: hold the phone at a fixed distance, and have `camd`
  dump both `scan.raw` (preview) and `scan_full.raw` (source) at
  several exposures spanning the useful range.
- Save each capture as `<target>_<exposure>_<resolution>.raw` plus a
  small JSON sidecar with the exposure, gain, luma_gain, and any
  decode result.

**Cost:** one session with the user, then everything else is offline.

**Done when:** `quirc-replay` decodes a known-good preview frame and
prints the expected payload.

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

**Done when:** we have a table of success rate per (resolution ×
threshold mode) over the corpus, and the winning combination is
identified.

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

The memory cost is fine (2 MB luma against a 32 MB heap). The design
the review proposes is right: **preview decode first, and only after a
few consecutive preview failures request one full-resolution decode**,
before falling back to an AF sweep. If a QR bounding box can be
obtained from the preview, decode only the corresponding
full-resolution crop — that is much cheaper than the whole frame.

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
Step 1  host harness + corpus          <- needs the user once
   |
Step 2  exposure ceiling (camd clamp)   <- 2 lines, provably safe
   |
Step 3  decoder thresholds (host)       <- no device needed
   |                                      yields: which decoder config
   |                                      actually decodes the corpus
Step 4  gain characterisation (device)  <- needs a lit target + user
   |
Step 5  percentile AE (native module)   <- informed by 3 and 4
   |
Step 6  full-res fallback (device)      <- only if step 3 says so
   |
Step 7  AF scheduling (device)          <- only if 2-6 leave focus as
   |                                      the limit
Step 8  entropy capture                 <- after 2 and 5
```

Steps 3 and 4 are independent of each other and can be interleaved.
Step 2 is independent of everything except that it should precede any
measurement.

---

## 4. What I expect the answer to be, and why

For the record, so it can be falsified:

- **Local thresholding will matter a lot** for a screen-displayed code
  in a dark frame, because a global threshold computed over the whole
  frame is dominated by the dark surroundings. The corpus will show
  this quickly.
- **Full resolution will matter** for dense codes, because the
  measurement above shows the source carries ~1.7–2.3× the edge
  energy per pixel that the preview does — the downscale is not
  throwing away information, it is destroying it.
- **The AE fix is necessary but not sufficient.** It removes a real
  bug (the controller commanding an exposure the sensor discards) and
  it makes the measurement honest, but on its own it will not make a
  dense code decode.
- **The AF is probably not the main problem**, as the review says. The
  coarse/fine sweep finds the peak reliably (steps 6–9 at arm's
  length) and the sharpness metric is reasonable for contrast-detect
  AF. What is wrong with it is *scheduling* — it fires when the real
  problem is binarisation.

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
