# Camera autofocus (DW9714 VCM)

Status 2026-11-05. The IMX179 on the hammerhead has a **DW9714 voice-coil
actuator** at I2C address `0x18` (`qcom,actuator@18`, HAL identity
`0x10002`), exposed as `/dev/v4l-subdev7` (`msm_actuator`). The lens is
parked near infinity at power-up.

The vendor sequence has been captured and replayed, but **`SET_INFO` and
real lens moves currently stall the capture stream**. Focus calibration
also needs a properly lit scene. Both are open.

## What the vendor HAL does

Captured on a diagnostic kernel (see "Tooling" below) while Android
CameraX opened the camera:

```
open
cfg type=0   GET_ACTUATOR_INFO
cfg type=1   SET_ACTUATOR_INFO
cfg type=2   SET_DEFAULT_FOCUS  dir=0 sign=1 dest=7  steps=7  inf=7
cfg type=3   MOVE_FOCUS         dir=0 sign=1 dest=7  steps=1
cfg type=3   MOVE_FOCUS         dir=0 sign=1 dest=12 steps=5
cfg type=3   MOVE_FOCUS         dir=0 sign=1 dest=17 steps=5
             ... 22, 27, 32, 37, 42 (steps=5) ...
cfg type=3   MOVE_FOCUS         dir=0 sign=1 dest=43 steps=1
cfg type=3   MOVE_FOCUS         dir=1 sign=-1 dest=9 steps=34
cfg type=3   MOVE_FOCUS         dir=1 sign=-1 dest=7 steps=2
             ... then another sweep up ...
```

Notes:

- **Convention:** increasing step → `dir=0, sign=+1` (towards macro);
  decreasing step → `dir=1, sign=-1` (towards infinity). `num_steps` is
  the distance moved, not the destination.
- The HAL moves the lens **while the sensor is streaming**, so this is
  not inherently incompatible with the capture path.
- `num_steps_inf_pos` is garbage (`-1359892448`) in the HAL's moves — it
  is left uninitialised. camd sets it to the step count instead.
- The initial hunt walks 7 → 43 in steps of 5, then settles back with
  larger jumps (34, then 2). This is a coarse sweep, not a
  contrast-detect search in the HAL.

## Captured actuator table

`actuator_info.bin` (parsed by `tools/parse_actuator_dump.py`):

| Field | Value |
|---|---|
| Total steps | 45 |
| Region size | 1 region |
| DAC codes per step | 9 |
| DAC width | 10 bit |
| Initial code | 169 |
| Init registers | `0x00ec`, `0x00a1`, `0x00f2`, `0x00dc` |

`cam/cam_actuator.h` carries the step constants used by camd.

## What camd does now

`cam/camd.c`:

- `af_bringup()` runs **right after sensor power-up, before the mode
  table is programmed** (`sensor_program_mode()`), matching the HAL's
  ordering. It replays `open → GET_INFO → SET_INFO →
  SET_DEFAULT_FOCUS(7)` and optionally one move.
- `actuator_move()` tracks the current step (`act_curr`, initially 7)
  and derives `dir`, `sign` and `num_steps` from the direction.
- `/data/krux/af` (0–4) bisects the sequence without rebuilding:

  | Level | Actions |
  |---|---|
  | 0 | Leave the actuator alone (default) |
  | 1 | `open` + `GET_ACTUATOR_INFO` |
  | 2 | + `SET_ACTUATOR_INFO` |
  | 3 | + `SET_DEFAULT_FOCUS(7)` |
  | 4 | + one move to `/data/krux/focus` (or step 7) |

- `/data/krux/focus` (0–44) pins a fixed step. When set, the startup
  sweep is skipped and the lens parks there.
- When no fixed step is set, a coarse + fine contrast sweep runs after
  the stream is up: 7 → 42 in steps of 5, then ±4 around the best, using
  an L1 horizontal-gradient score over the centre half of `latest_y`
  (`af_sharpness()`). It is a placeholder until the vendor sequence and
  calibration are settled.
- Lens control is **opt-in** so the camera stays usable while the
  interaction is being fixed.

## Bisect results (2026-11-05, dark scene)

| Level | Result |
|---|---|
| 1 (`open` + `GET_INFO`) | Pipeline survives; 106 frames, sweep completes |
| 2 (+ `SET_INFO`) | Stream stalls after ~2 frames |
| 3 / 4 | Same stall |

`msm_actuator_parse_i2c_params` logs repeated
`failed: i2c table index out of bound` (12 lines, one per register
write) when a move is attempted. That is the table check against
`total_steps`, so the parsed table is not what camd thinks it sent.

Two caveats from the last runs:

- The bundled boot image restores its own camd, and one run clearly used
  an older binary (the actuator log showed `cfg 1 → cfg 3`, no `cfg 2`).
  Always verify `/opt/krux/camd` / `/data/krux/camd` matches the build
  under test.
- The `actuator init: steps=... region=...` diagnostic line was not
  present in the last dmesg, so the parsed `total_steps`/`region_size`
  are still unconfirmed. That is the first thing to check next.

## Next steps

1. Confirm the boot image carries the newest camd (compare hashes or
   check for the `[af] actuator configured:` line after `cfg type=2`).
2. With `af=2`, read `dmesg | grep 'actuator init:'` and compare
   `steps`/`region`/`regs` with the captured table. If `steps` is 0 or
   garbage, the `msm_actuator_set_info_t` layout in `camd.c` does not
   match the kernel and must be corrected.
3. Once `SET_INFO` no longer stalls, calibrate with light: set
   `/data/krux/focus` to 7/15/25/35 at the working distance (~40 cm),
   compare sharpness, and pick the best.
4. Replace the coarse sweep with the vendor-style hunt and verify QR
   decode at that step before making lens control the default.

## Tooling

- `linux/drivers/media/platform/msm/camera_v2/sensor/actuator/msm_actuator.c`
  — `act_log()` writes the call sequence to
  `/data/local/tmp/actuator_seq.log`; `actuator init:` prints the parsed
  parameters.
- `tools/parse_actuator_dump.py` — decodes `actuator_info.bin` into the
  step table.
- Diagnostic kernel boot (Android) then open the camera; pull
  `/data/local/tmp/actuator_seq.log` and `actuator_info.bin`.
- Stub boot for camd tests: `fastboot boot <image>`; kill/restart of
  camd is not reliable, so use a cold boot per camera session.

## 2026-11-05 — the stall was zero damping

The `SET_INFO`/stream stall was not the actuator table.  `camd` sent an
all-zero `ringing_params` array; `msm_actuator_move_focus()` calls
`actuator_write_focus()` with `damping_step = 0`, whose loop never
advances:

```c
for (next = curr + sign * damping_step;
     sign * next <= sign * boundary;
     next += sign * damping_step)
```

With `damping_step = 0` it writes the same position forever, floods
`msm_actuator_parse_i2c_params()` with `i2c table index out of bound`
(5,889 lines in one run) and stalls the CCI/stream after two frames.
Setting `damping_step = 1` (and `damping_delay = 0`) fixes it.

Verified on a lit 60x60 mm QR at 40 cm:

```
[af] actuator configured: 45 steps, code 169
[af] step 7 score 67824
[af] step 12 score 67542
[af] step 17 score 69579
[af] step 22 score 69027
[af] step 27 score 66540
[af] step 32 score 43602
[af] step 37 score 29901
[af] step 42 score 21113
[af] focus step 19
```

84 frames during the sweep, no stall.  `camd` now re-reads
`/data/krux/focus` at every camera session, so the fixed step can be
changed without restarting camd.

A second sweep with a brighter scene produced much larger scores
(342k-440k) and picked **step 14**.  The focus step is scene/light
dependent; the sweep is the right default, with `/data/krux/focus` for a
fixed value.

The remaining QR issue at that moment was the **scene brightness**:
a direct 720x960 luma frame from camd had mean 30 and max 52 even with
VMAX 0x09d6 and exposure 0x09c0, so the QR was not decodable.  That is a
lighting/aim question, not a focus or pipeline question.  The camera
preview needs the 32 MiB heap (the 720x960 RGB565 preview alone is
1,382,400 bytes); the old 8 MiB heap raised `MemoryError` here.

## Final sweep tuning (2026-11-05)

With the QR in view the coarse sweep scores peaked at **step 21**
(1,190,711) and 24 (1,124,486), but the first fine sweep parked at 19
and the image was still blurry.  A QR decoded immediately at a fixed
**step 21**, confirming 21 is the focus for this 40 cm scene.

The fine sweep now keeps the coarse best unless a fine step beats it by
**5%**; the gradient metric is noisy, and the coarse peak is the real
focus.  The sweep starts at 0 and steps by 3, so a cold lens reaches 21
after seven moves and the QR can decode mid-sweep (the page exits and
leaves the lens at 21).  If the sweep completes, it parks at 21.

## Manual focus/exposure (2026-11-05)

The QR page now has a manual exposure path and an on-demand AF sweep:

- **Volume +/−** and the on-screen `−`/`+` buttons adjust exposure
  (`0x100` per step); the first adjustment locks the AE.
- **Power** asks camd to run the AF sweep again (`R` socket command),
  so the lens can be refocused after moving the phone without a reboot.
- `/data/krux/unsharp` selects the preview/QR sharpening: `0` off,
  `1` `2*luma-blur`, `2` softer `(3*luma-blur)/2`.  Off keeps
  high-density modules from looking “thin”.
- The default is now `af=2` with no fixed `focus`, so the sweep runs at
  camera start.  A fixed `focus` still skips it for speed.

The AE is **one-shot**: it runs at camera start only.  There is no
continuous per-frame exposure loop; after that the manual `VOL+/-`
(and the on-screen `+`/`-`) controls the exposure and locks it.  For
detail on high-density codes, `luma_gain=1` (no digital gain) and
`unsharp=0` are the useful starting values.  **Power** runs the AF
sweep even when a fixed `focus` is set, so a fixed 40 cm step can be
used for speed and re-focused on demand.

## 2026-11-05 — the coarse peak is not the focus

The focus metric is `af_sharpness()`: the sum of `|dx| + |dy|` over a
sparse grid in the centre 50% of the 720x960 luma frame.  It is not
normalised, so only compare scores within one sweep.

The coarse sweep can pick a step the lens cannot reproduce.  On a
near-full-screen HD PTSB QR the coarse step 15 scored about 1.5M, but
the fine sweep's step 15 scored only about 146K after the lens had
moved back from 44.  The QR decoded while the fine sweep passed steps
8-13 and became unreadable once it parked at 15.

The fine sweep now:

- searches a **+/-8 step** window around the coarse peak,
- starts at the low end and moves forward, so the VCM's backlash is
  taken up before each score,
- keeps the **fine peak**, not the coarse one, when it is at least
  half the coarse score,
- approaches the chosen step with a one-step forward move
  (`best-1 -> best`), the same move that produced the fine peak.

Verified with the control socket: fine peak step 11 (1.66M), final
`10 -> 11`, hold scores 1.55M and 1.50M over the next seconds, and the
host decoded the 720x960 frame as `UR:CRYPTO-PSBT/...` both right after
the sweep and 5 s later.  The device logged `QR exit complete=True
parsed=1 total=1`.

The sweep now takes a few seconds (coarse + 17 fine steps, two frames
per step).  A per-scan refocus is still run; Power also re-runs it.
