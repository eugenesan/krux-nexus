# Local Agent Prompt: Review and Improve Camera AF/AE and Dense-QR Detection

You are working in the `krux-nexus` project tree. Review the current camera implementation and test a prioritized set of changes intended to improve dense/high-definition QR decoding on the Nexus 5 rear camera.

## Important operating constraints

- Treat the checked-out project tree as the only source of truth.
- Do not use symlinks.
- Never flash the device. Use `fastboot boot` only.
- A cold boot is required for a clean camera session because the CPP pipeline cannot currently be torn down and recreated reliably in one boot.
- Do not change VMAX or other sensor geometry/timing registers while streaming. A previous live VMAX write caused a reboot.
- Preserve entropy-capture behavior. Software luma gain must remain disabled in `ENTROPY_MODE`, and the entropy image must not be reshaped by QR-specific controls.
- Keep changes narrowly scoped, reversible, and committed separately by experiment.
- Record exact commands, commit IDs, device logs, test inputs, and results.

## First task: verify platform facts

Before changing code, resolve an apparent inconsistency in the brief:

- Nexus 5 `hammerhead` is normally associated with Qualcomm MSM8974.
- The brief says `MSM8939 (hammerhead / Nexus 5)`.

Confirm the actual target SoC and correct documentation/comments if this is only a typo. Do not make hardware assumptions until verified from the tree, device identifiers, DTS, and kernel configuration.

## Current pipeline

```text
Sony IMX179, rear-binned 1640x1232
    -> CAMIF_RAW
    -> offline Qualcomm CPP
    -> V4L2
    -> cam/camd.c
    -> 720x960 portrait luma preview
    -> MicroPython image object
    -> k_quirc via image.find_qrcodes()
```

Current relevant behavior:

- Sensor exposure is controlled by IMX179 registers `0x0202/0x0203`.
- Analog gain is controlled by `0x0204/0x0205`.
- VMAX is `0x04e4`, so normal maximum coarse integration should be verified as approximately `VMAX - 4 = 0x04e0`.
- Current configured `MAX_EXPOSURE` is `0x09d6`, which exceeds the active frame length.
- AF uses a coarse/fine DW9714 actuator sweep and a sum-of-gradients sharpness metric.
- AE is frozen during an AF sweep, but scheduling can still race between sweeps.
- `k_quirc` currently uses a global Otsu threshold.
- Source luma exists at 1640x1232, but normal decoding uses downscaled 720x960 luma.

## Overall hypothesis and priority

Do not assume autofocus is the main remaining blocker. The current AF design is already structurally reasonable for contrast-detect AF.

Test these hypotheses in this order:

1. Global binarization is the largest dense-QR limitation.
2. Loss of source resolution is the second-largest limitation.
3. AE has invalid limits and a brittle measurement, but may be less important than binarization and resolution for screen-displayed QR codes.
4. AF scheduling and metric normalization may still need refinement after the first three items are measured.

Do not combine all changes at once. Establish a baseline, then test one independent variable at a time.

---

# Phase 0: Build a reproducible benchmark

Create a repeatable test protocol before modifying algorithms.

## Test QR set

Use several fixed QR samples that cover:

- Low-density QR, approximately Version 1 to Version 5.
- Medium-density QR.
- Dense/high-version QR representative of the failing workload.
- At least two physical/display sizes per payload.
- Static and animated QR, if animated QR is a real application requirement.
- Screen-displayed and printed QR if both are supported.

Record for each code:

- QR version or module dimensions.
- Error correction level.
- Payload length.
- Displayed or printed physical size.
- Viewing distance.
- Approximate QR width in source pixels and preview pixels.
- Approximate pixels per module.

## Fixed capture conditions

For each test run, record:

- sensor exposure register value and read-back;
- sensor analog-gain register value and read-back;
- software `luma_gain`;
- actuator step;
- source and decode resolution;
- decoder threshold mode;
- luma histogram statistics;
- clipped pixel count or percentage;
- AF score;
- decode success/failure and decode latency.

Save representative source-luma frames, preview-luma frames, and thresholded images. Give each artifact a filename that encodes the configuration.

## Metrics

At minimum report:

- success rate over 20 or more attempts per configuration;
- median and p90 time-to-first-decode;
- source and preview pixels per QR module;
- ROI luma percentiles P5, P50, P90, P95, P99;
- percentage at 0, below 8, above 247, and at 255;
- focus score at the selected actuator position.

Do not judge changes from one visually inspected frame.

---

# Phase 1: Correct the exposure model

## 1.1 Verify actual integration limit

Inspect the IMX179 mode table and sensor documentation available in the tree. Confirm:

```text
VMAX = 0x04e4
maximum legal coarse integration = VMAX - margin
```

Verify the margin used by this mode rather than assuming four lines solely from the brief.

Read back `0x0202/0x0203` after representative writes below, at, and above the limit. Determine whether the sensor clamps, ignores, wraps, or substitutes the value.

## 1.2 Fix the controller limit

If read-back confirms `0x04e0` as the maximum, set the normal streaming limit accordingly:

```c
MAX_EXPOSURE = 0x04e0;
```

Better: derive the limit from the active mode's VMAX and integration margin instead of duplicating a constant.

Pseudo-code:

```c
uint16_t exposure_limit(const struct sensor_mode *mode)
{
    return mode->vmax - mode->coarse_integration_margin;
}
```

Clamp before sending the command, and update the AE's internal state from the value actually accepted/read back when practical.

## 1.3 Do not raise VMAX yet

Do not implement runtime VMAX changes in this phase. First measure performance with a truthful 1248-line ceiling. Raising VMAX changes frame timing, may affect CSI/CPP state, and previously triggered a crash in a live pipeline.

If longer integration is eventually required, implement it only as a separately initialized sensor mode selected before stream-on, with matching frame-duration and buffer timing assumptions throughout the pipeline.

## 1.4 Add anti-windup behavior

The AE must stop integrating error when a control is at its effective limit.

Pseudo-code:

```c
if (too_dark) {
    if (exposure < exposure_max)
        increase_exposure();
    else if (gain_index < gain_count - 1)
        increase_gain();
    else
        report_ae_limited_dark();
}
```

Log when the scene is outside available hardware authority. Do not repeatedly command an impossible exposure.

---

# Phase 2: Characterize gain rather than assuming it is linear

The existing measurements show nearly no response until the top of the configured range, followed by a large jump near `0xff`. Determine whether this is:

- the actual IMX179 analog-gain encoding;
- a register-width or write-format issue;
- an invalid lower range;
- automatic processing elsewhere in the pipeline;
- or a measurement artifact caused by a nearly black target.

## Procedure

Use a stable, uniformly illuminated target with meaningful signal, not a dark desk dominated by noise.

At a fixed exposure well below clipping:

1. Sweep every gain code, or at least increments no larger than 4.
2. Read back the gain registers.
3. Capture multiple frames per setting.
4. Record median and variance of ROI P50, P90, P95, and mean.
5. Plot register code against measured luma response.
6. Repeat at a second exposure to detect interactions.

## Controller representation

If response is non-linear but monotonic, use either:

- the sensor's documented gain-code conversion, or
- a calibrated lookup table of useful gain states.

Example:

```c
struct gain_state {
    uint16_t reg;
    float measured_multiplier;
};
```

AE should choose the next calibrated state based on measured multiplier, not add `0x18` blindly.

Do not use software luma gain to hide a bad sensor-gain model. Keep it as a last-resort display/decoder aid and track separately from physical exposure.

---

# Phase 3: Replace the brittle bright-pixel AE measurement

The present rule, "pixels within 40 levels of maximum," collapses to whole-ROI mean when the maximum is below 40 and becomes unstable when a few hot/noisy pixels define the maximum.

## Implement histogram percentiles

Build a 256-bin histogram of the luma ROI. From it compute at least P50, P90, P95, P99, clipped-high fraction, and dark fraction.

Pseudo-code:

```c
void luma_histogram(const uint8_t *y, int stride,
                    int x, int y0, int w, int h,
                    uint32_t hist[256]);

uint8_t percentile(const uint32_t hist[256],
                   uint32_t total, unsigned percent);
```

## Initial QR-oriented target

Test a controller based on a high percentile rather than the brightest cluster. Start with a hypothesis such as:

```text
P95 target: 200 to 225
high-clipped fraction limit: 1% to 3%
```

These are starting points, not hard-coded truths. Select the final values from the benchmark.

For screen QR codes, preserve white/black separation rather than maximizing brightness. A target of 225 may be too aggressive if it causes broad clipping, screen PWM bands, blooming, or loss of module boundaries.

## ROI handling

Test these ROI strategies independently:

1. Existing middle-half ROI.
2. Center-weighted histogram.
3. QR-candidate ROI if finder-pattern localization can be obtained before full decode.

Do not let a small number of extreme pixels control exposure.

## Stability controls

Add:

- a dead band;
- a minimum number of frames between changes;
- asymmetric attack/release if necessary;
- a settled counter requiring several consecutive in-range measurements;
- anti-windup at exposure/gain limits.

Use actual read-back settings in logs.

---

# Phase 4: Test local thresholding before extensive AF redesign

This is the highest-priority decoder experiment.

## 4.1 Enable the existing k_quirc alternative first

Locate and test the existing `K_QUIRC_BILINEAR_THRESHOLD` path. Build an A/B switch so the same saved luma frame can be decoded with:

- current global Otsu;
- existing bilinear/per-quadrant threshold;
- any existing adaptive-threshold path, if complete and safe.

Do not initially replace the decoder. First establish whether local thresholding substantially improves the existing failing corpus.

## 4.2 Add an offline replay harness

A local/off-device test program should read saved raw luma frames and run every threshold mode plus `k_quirc`. It should report:

```text
filename
resolution
threshold mode
decode success
payload or error
time spent in binarization
time spent in decode
```

This enables algorithm work without repeated cold boots.

## 4.3 Evaluate a ZXing-style HybridBinarizer port

If the built-in bilinear path is insufficient, implement a native local block threshold inspired by ZXing's `HybridBinarizer` behavior:

- small blocks, initially 8x8;
- local black-point estimation;
- neighborhood smoothing, initially 5x5 blocks;
- careful treatment of low-dynamic-range blocks;
- direct operation on the native luma plane.

Do not implement this in Python. Keep it in the native image/decoder module to meet the frame-rate and heap constraints.

Avoid copying incompatible licensed code verbatim. Reimplement the algorithm from its documented behavior if license compatibility has not been confirmed.

## Acceptance criterion

Prefer the simplest local-threshold implementation that materially improves dense-QR success without unacceptable false positives, CPU use, or latency.

---

# Phase 5: Restore a full-resolution fallback

The source luma already exists in `camd`; normal decoding discards substantial linear resolution by decoding the 720x960 preview.

## Desired behavior

Keep the preview path for responsiveness, but after a small number of failed preview attempts, request one source-resolution luma decode before initiating a costly AF sweep.

Proposed order:

```text
preview decode
preview decode
preview decode
    -> full-resolution decode attempt
        -> success: return result
        -> failure: consider AF or AE action
```

Do not run full-resolution decode continuously until performance and memory cost are measured.

## Implementation investigation

Review the removed `sensor.snapshot_full()` and camd `H` command in the referenced history. Restore only the minimum stable functionality needed to expose an immutable full-resolution luma frame.

Avoid unnecessary RGB conversion and avoid copying the full frame more than required. Calculate the exact memory cost:

```text
1640 * 1232 = 2,020,480 bytes for one 8-bit luma plane
```

Account for decoder work buffers and the configured 32 MB MicroPython heap.

## Additional optimization

If a candidate QR bounding box can be obtained from preview, consider decoding a full-resolution crop corresponding to that region, with a quiet-zone margin, instead of the entire source frame.

## Acceptance criterion

Measure success rate and p90 latency on dense codes. Confirm that full-resolution fallback improves pixels per module enough to justify its memory and CPU cost.

---

# Phase 6: Stabilize AF/AE sequencing

Keep AE frozen during an AF sweep. Do not compare raw gradient scores captured at different exposure/gain settings.

## Recommended state machine

```text
AE_ACQUIRE
    -> require N consecutive settled frames
AF_SWEEP
    -> lock exposure/gain
    -> move lens
    -> discard settling frames
    -> score a frame, preferably average multiple scores
    -> complete coarse/fine sweep
AF_LOCKED
    -> resume slow AE follow
    -> invalidate focus only on meaningful scene/quality change
DECODE
```

Use one owner for this state machine. Avoid independent firmware and daemon decisions that can race.

## AF score normalization

The current `sharpness / mean` value may become unstable in dark/noisy images and may reward noise. Test, do not assume, alternatives such as:

```text
sum of gradients above a noise floor
Tenengrad/Sobel energy
normalized gradient energy using robust luma range
```

For example:

```c
if (abs(dx) > noise_threshold)
    score += abs(dx) - noise_threshold;
```

Estimate `noise_threshold` from dark-region or frame-to-frame noise. Keep the existing metric as the baseline.

## Multi-frame scoring

At each actuator step:

1. discard the configured lens-settling frames;
2. score 2 or 3 frames;
3. use the median score;
4. reject frames showing strong screen-PWM band changes or exposure instability.

This costs time, so benchmark it against the current single-frame score.

## Sweep trigger

Test shorter failure thresholds than 15 frames, for example 3 to 5 failed preview frames, but only after AE has settled. Before sweeping, try local thresholding and a full-resolution decode. This avoids refocusing when the actual problem is binarization or sampling resolution.

Suggested decision order:

```text
preview decode fails several times
    -> local-threshold/full-resolution attempt
    -> if still failing and focus score is weak: local AF sweep
    -> if local sweep weak: full AF sweep
```

Limit repeated sweeping unless the image quality metric or scene has materially changed.

---

# Phase 7: Noise-aware behavior

The sample low-light patch indicates substantial sensor noise. Increasing analog or software gain may improve brightness while making dense modules less separable.

Measure signal-to-noise behavior at each useful exposure/gain state. For a static target, estimate:

- spatial noise in nominally uniform black and white regions;
- temporal noise across consecutive frames;
- white-minus-black module contrast;
- contrast-to-noise ratio.

Prefer the exposure/gain setting that maximizes QR module separability, not merely P95 brightness.

If frame rate permits, test a small temporal denoise only on stationary scenes, such as median/average of two or three aligned luma frames. Reject this approach if motion ghosting or latency harms scanning. Do not apply smoothing that erases small modules.

---

# Required experiment order

Run and report these experiments separately:

1. Baseline current tree.
2. Correct exposure ceiling only.
3. Existing bilinear/local threshold only.
4. Full-resolution fallback only.
5. Local threshold plus full-resolution fallback.
6. Percentile AE with calibrated exposure/gain limits.
7. AF scheduling/state-machine changes.
8. AF metric changes only if preceding results show focus remains limiting.

For each experiment, preserve before/after artifacts and benchmark results.

---

# Decision criteria

Use evidence to answer:

1. Does local thresholding improve dense QR decoding on the saved failing images without any camera-control change?
2. Does full-resolution luma decode improve success because of increased pixels per module?
3. After correcting exposure limits, does AE converge without command saturation or oscillation?
4. Is analog gain actually monotonic after proper characterization?
5. At the actuator-selected best position, are dense modules optically resolved in source luma?
6. When decoding fails, is the limiting factor blur, noise, clipping, local illumination, or insufficient sampling?

A useful diagnostic classification for each failed frame is:

```text
FOCUS_LIMITED
EXPOSURE_CLIPPED
NOISE_LIMITED
THRESHOLD_LIMITED
RESOLUTION_LIMITED
NO_QR_IN_ROI
UNKNOWN
```

Implement this initially as offline analysis if runtime classification is too expensive.

---

# Deliverables

Produce:

1. A short technical report with measured findings and ranked bottlenecks.
2. A CSV or Markdown table of benchmark results.
3. Saved representative source, preview, and thresholded luma frames.
4. Separate commits for:
   - exposure-limit correction;
   - gain characterization/support;
   - percentile AE;
   - local thresholding;
   - full-resolution fallback;
   - AF scheduling or metric changes.
5. A final recommendation identifying the smallest reliable production change set.

Do not merge speculative complexity. Prefer a small, measured combination such as:

```text
truthful exposure ceiling
+ robust percentile AE
+ local thresholding
+ on-demand full-resolution decode
+ existing coarse/fine AF
```

unless benchmark results demonstrate that a different combination is superior.
