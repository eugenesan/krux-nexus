# Camera state (2026-11-11)

**This is the camera's current state.**  It supersedes the earlier
snapshots: the camera runs the vendor's processed preview path
(PIX_VIEWFINDER through the CPP), the preview has no stripes, and both
the QR decoder and the camera-entropy capture work through it.  The
history of how it got here is below, in the sections that were written
at the time; the section "The PIX path works (2026-11-11)" at the end
is the current one.

This document was `milestone-2026-11-10-snapshot2.md`; it took the name
of `cam-state.md` so that there is exactly one canonical camera document.
The previous `cam-state.md` is in [archive/](archive/cam-state-2026-11-05.md).

## The auto-exposure measures P95 (2026-11-12)

The AE's brightness measure was the mean of the pixels within 40 levels
of the frame's brightest pixel.  That margin is absolute, so in a dim
scene the measure collapses to the plain mean, and a few hot pixels
move it.  On the device it read 151 while 53% of the frame was
clipped: the controller walked the exposure to the floor and back --
the pumping that is visible on the preview.

The native image module now computes P50/P90/P95/P99 from a 256-bin
histogram over the measured region, and how many pixels it measured
(`image_maixpy.c`).  The controller uses **P95**, targets **210** (the
review's 200-225), and treats a **clipped fraction above 3%** as
over-exposed whatever P95 says.  The old 0.75 measurement filter is
gone: P95 is stable, and the filter's lag was turning every correction
into an overshoot.  The step is capped at +-20% with a 0x30 floor.

The log line is now `Camera AE p95=... clip=...% exposure=... gain=...
luma=...`, so a hunting controller can be read directly.

Two follow-on fixes from testing it:

- **The exposure ceiling in PIX.**  The QR ceiling was chosen from the
  preview geometry (`width >= 2160` = full sensor frame).  The PIX
  preview is 1280x960, so the full-sensor PIX path was classified as
  binned and capped at `0x04e0` -- half the range.  A dark scene was
  stuck there with the gain at 0xff and never came up (the device log
  showed p95 27-123 at the cap).  The QR path now raises the frame
  length to the vendor's 2518 lines (as entropy already did) and uses
  `0x09c8`; the follow raises the digital luma gain if the picture is
  still dark with the exposure and analog gain at their limits.
- **The follow settles and stays settled.**  Once inside the dead band
  for three measurements it locks the exposure, and only wakes when
  the scene leaves twice the band for three measurements; a step
  needs the error on the same side for two measurements.  Entropy's
  mean is smoothed again (it is the noisy metric); P95 stays raw.
  This is what stopped the hunting on the entropy page, which calls
  the follow on every frame.
- **Entropy's own cycle.**  The log showed mean 87 while 86% of the
  frame was clipped: the exponential mean filter lagged, the 30%
  clipped-high step overshot, and the lock's `clip <= 3%` condition
  could never be met by a paper QR, whose white modules legitimately
  clip.  Entropy now uses a median of the last three means (no lag),
  no clipped-high guard (the guard is for the QR P95 metric), a 15%
  step, and a wider band.  Verified: entropy converges to mean 125 and
  locks; a bright scene descends monotonically; QR converges in 7
  steps to p95 202 and locks.

## The on-demand stream was tried and reverted (2026-11-12)

The sensor was stopped when nothing had asked for a frame for about
three seconds and started again by the first request.  The switch was
the sensor's own stream bit (`0x0100`), the same pause the autofocus
makes around a lens move; every node, buffer and ISP/CPP configuration
stayed open.

It cut the idle cost as intended -- system time ~38 % -> ~6 %, and the
thermals 7-8 C cooler -- but it **broke the preview**.  After a pause
and resume the ISP/CPP came back out of frame sync: random horizontal
overlap whose width changed and drifted across the preview every few
seconds, and the auto-exposure pumping as it chased the bad frames.
The brief pause the autofocus uses is fine; a long one is not.

**It is reverted.**  `cam/camd.c` is the known-good build again (md5
`99ef6da3275fdb882efe9cbf18ed67fe`) and the stream runs continuously.
The idle heat is therefore back, and it is an open problem: the way to
solve it is at the process/pipeline level (a clean stop/start of the
whole daemon, or a very low idle frame rate), not by pausing the sensor
under a running ISP.

## The commits

| tree | commit | what it is |
|---|---|---|
| `krux-nexus` (main) | the commit that adds this file | the docs and the submodule pointers |
| `linux` | `cf9fd9c2edf` | the camera_v2 fixes (buffer hold, divert, input index) |
| `krux` | `676cceff` | the Krux port: the QR exposure ceiling |
| `krux/firmware/micropython` | `ef4593e60` | the unix port: the display, after the ping-pong revert |

To come back here:

```
cd /home/user/Develop/src/krux-nexus
git checkout <the commit that adds this file>
git submodule update --init --recursive
```

The submodule checkouts are recorded in that commit: `linux`
`cf9fd9c2edf`, `krux` `676cceff`, and inside it `krux/firmware/micropython`
`ef4593e60`.

## The artifacts, and how they are built

| artifact | built by | md5 |
|---|---|---|
| `cam/camd` | `bash scripts/cam/build-camd.sh cam/camd` | `3c6eea90ad5b77264f1c380b3f7959f8` |
| `rootfs/micropython-arm32` | `bash scripts/build/build-micropython.sh` | `ab9e221f867f798d0fdd8db40724d3cc` |
| the kernel | `bash tools/build-kernel.sh --tree linux --out /tmp/opencode/ko` | zImage-dtb `6b5853ad8ab3bac41e871de9948752b2` |
| the boot image | `bash rootfs/make-boot-image.sh artifacts/twrp-3.7.0_9-HH.R.17-MADV_WIPEONFORK.img /tmp/opencode/ko/arch/arm/boot/zImage-dtb /tmp/boot-dev.img` | `a6d9d68805775d1f7c41323115669aa4` |

The kernel source is the authority: `linux` is clean at `cf9fd9c2edf`
and the device reports
`3.4.113-🇺🇦-g3f7bed3f2d6-09404-gcf9fd9c2edf`.  The kernel image itself is
not bit-reproducible (the build embeds a timestamp), so the md5 above
identifies this build, not every build of the same source.

Deploy: `ANDROID_SERIAL=0879524b0068abca bash scripts/deploy-firmware.sh`
(pushes `camd`, the interpreter, the Python and restarts the firmware).
Never flash: `fastboot boot /tmp/boot-dev.img` only.

## The device's state

```
/data/krux/state/mode          1     full resolution, 3280x2464
/data/krux/state/cycle         1     the vendor's per-frame ISP cycle
/data/krux/state/stream        0     CAMIF_RAW: the ISP's raw into the CPP
/data/krux/state/capture_cfg   1     the vendor's capture ISP configuration
/data/krux/state/fov           0     2160x2160 centre crop -> 1080x1080
/data/krux/state/profile       0     the daemon's per-phase timing
```

With those, the daemon prints
`[mode] window 2160x2160 at (560,152) -> preview 1080x1080`.

Health after this session: `process queue full: 0`, `no frame after: 0`,
no drops or stalls in the daemon log.  The daemon is `3c6eea90`, the
interpreter `ab9e221f`, both the built artifacts above.

## What was verified

All three endpoints, on this build:

| endpoint | result |
|---|---|
| preview | live 1080x1080; two captures 1 s apart differ in **82.3 %** of their bytes, means 116.5 / 117.3 |
| QR | decodes the full `UR:CRYPTO-PSBT/...`; confirmed via **Tools -> Datum Tool -> Via Camera** once the code filled the frame, and from a computer screen and the display phone at low brightness |
| entropy | capture completes: **5.3 bits/px, pixels deviation index 51** |

Also verified on this build:

- **Dark-start auto-exposure.** With the display phone's screen off, the
  QR page's exposure climbed to `0x09C8` (gain `0x00FF`); with the screen
  on it walked back down (`0x074F`, `0x06FA`, ... `0x0320`) and settled.
- **Focus.** The first sweep of a scan is a full one and finds the code
  (step 13 in the earlier QR test, step 3 on the Datum page); the code
  decodes without a tap.
- **Buffer health.** No queue-full drops and no frame stalls in the
  daemon log.

## What is still wrong: the right-hand preview stripes

The preview shows two vertical bands at the right edge.  They are
**not in the camera image**:

- the shared frame the QR decoder and the entropy read held a constant
  mean (90) for twenty seconds, with no stripes and no brightness
  change;
- the framebuffer plane the display module had just written is clean
  at 1:1.

They are introduced between the framebuffer and the glass.  The panel is
a command-mode DSI panel (`msm_fb_type` = `mipi dsi cmd panel`), and the
MDP3 path re-points the DMA rather than copying: the panel's memory is
rewritten from a framebuffer plane while the panel scans it, so the
right-hand bands can show a newer frame than the rest.

Drawing into the other framebuffer plane (the "ping-pong") was tried
twice.  The second attempt also seeded the off-screen plane and clamped
to the two planes the framebuffer really holds.  It did not remove the
tear, and it added a ~10 s whole-preview brightness swing while the
camera frame stayed constant, so it was reverted (`ef4593e60`).  A real
fix is a display-level partial update (write only the 1080x1080 image,
~3.5 MB) or a video-mode panel; both are kernel/display changes and are
deliberately not part of this snapshot.

The QR and the entropy never see any of it.

## The CPP does not scale to 1280x960

A delivered full-resolution buffer is a native, unscaled Bayer frame:
the 2x2 sites measure 61.9 / 89.9 / 90.1 / 73.7 (the RGGB signature) and
the diagonal neighbour difference (10.0) is far below the orthogonal one
(22.4).  The CPP is a pass-through of the sensor's 3280x2464, not the
vendor's 1280x960 scaler.  The daemon therefore does the one resample:
by default the centre 2160x2160, halved exactly to 1080x1080.  Setting
`fov=1` uses the whole frame, downscaled once to 960x1280, but a dense
code is then only ~2 px/module in the preview and the finder pass loses
it, so `fov=0` is the default.

## The display phone

The LG `LGL322DL71cd2c3` shows the Sparrow UR from
`/sdcard/Download/sparrow.png` in the Gallery, `svc power stayon true`,
brightness 120.  The phone must be close enough for the code to fill a
good part of the frame: this is a dense (version-40-ish) code, and at
arm's length it is only ~200 px wide in the preview, which the finder
pass cannot read.

## Changes since the first snapshot

- The preview publishes raw-stream frames instead of rejecting them for
  a missing chroma plane (`32c2508`); that was why the full-resolution
  preview froze.
- The QR page's first focus sweep of a scan is full, not cached
  (`78dc874e`); the cached sweep refined a previous scan's step and the
  code never came into focus.
- The box downscale divides 3-pixel boxes too (`f2a0487`); the
  whole-frame path needed it and produced a light grid without it.
- The full-resolution window is a state knob, `fov` (`f2a0487`).
- The QR exposure ceiling follows the sensor's frame length (`676cceff`);
  the binned cap left a dark start stuck at half the exposure.
- The display ping-pong was tried and reverted (`1d8f43b62`,
  `ef4593e60`).

## The stripes are in the camera image, not the display

The diagnostic (`probes/tear.c`) draws a frame-number pattern into the
same framebuffer with the same `MSMFB_DISPLAY_COMMIT` the firmware uses,
and pans a detailed scene across it.  Two variants were run:

- the whole 1080x1920 panel, copied with `memcpy` (5.6 ms per frame);
- exactly the preview's draw: a 1080x1080 image at y=420, flipped in
  both axes and converted pixel by pixel (3.9-20 ms per frame).

**Neither tore**, at a 20 ms frame interval.  The horizontal "seams"
first seen were the test pattern's own 384 px grid lines; removing the
grid removed them.  So the display commit, the geometry, the flip and
the per-pixel conversion are not the cause.

The tear is in the image the firmware receives.  Capture of the shared
frame through the same seqlock reader the pages use
(`image.Image.from_shared()`), while the display phone's brightness
stepped, gives a column table: the **leftmost 10-20 % of the preview
image** changes one brightness step **about six frames (≈1.8 s) before
the rest**.  The geometry maps that band to the source:

- the daemon publishes the rotated preview, `col = source y`;
- the display flips x, so the glass's right edge is the image's left
  column, i.e. the **top rows of the sensor window**;
- the vendor message's first CPP strip covers exactly those rows.

So the right-hand stripes are the **first strip of the CPP output**
being fed from a different frame (or not rewritten) while the rest of
the frame is current.  The display is innocent; the work is in the CPP
strip path, not in `lcd_bcm.c`.

### Next options

1. Confirm in the raw delivered window: dump `src_y` twice across a
   brightness step and compare the top strip's rows with the rest.
2. Inspect the message's first strip: `dst_start_y`/`dst_end_y` and the
   postscale crop, and the kernel's per-frame input index, to see
   whether the first strip's destination is left unwritten or its input
   is a previous buffer.
3. Workaround if the geometry is hard: shift the preview window down so
   the first strip's boundary is outside the crop (costs a little
   vertical field of view).

## The vendor message's five strips overlap

`cam_chromatrix.h` is the vendor's 266-word message unchanged.  The
kernel hands it to the CPP firmware as-is; the kernel patches only the
frame's input index (from the ISP's divert) and takes the output buffer
from the buffer pool.  The strip rectangles therefore come from the
message, not from our full-resolution window.

Each strip is 27 words at `130 + 27*i`.  The destination rectangle is
the packed pairs at `+21..+26`.  Decoded, the five strips are:

| strip | dst x      | dst y      |
|-------|------------|------------|
| 0     | 426 .. 959 | 0 .. 430   |
| 1     | 430 .. 959 | 0 .. 434   |
| 2     | 429 .. 959 | 0 .. 429   |
| 3     | 319 .. 479 | 0 .. 319   |
| 4     | 341 .. 479 | 0 .. 319   |

They **overlap**: every strip starts at y=0, three cover x 426..959 and
two cover x 319..479, so the union is x 319..959, y 0..434 — not a
tiling of the frame.  The header's scale groups (words 43-46, 51-54,
59-62) map that space onto the output, which is why the picture looks
complete; the overlap is at the **top** of the source, and the preview's
rotation and horizontal flip put it on the glass's right edge — the
band.

The rows outside/inside the overlap are also written by different
strips, and the output buffer comes from the pool with the previous
frame still in it, so a strip that is not rewritten shows an older
frame.  Measured on the raw delivered window with a uniform exposure
change, the top 10 % and the rest do not track one another (`hi1` top
188 / middle 100, `hi2` top 19 / middle 106), which is that mix.

### Options

1. **Capture the vendor's full-resolution CPP message.**  The harness
   that produced `cam_chromatrix.h` exists; capturing in the full-res
   mode gives the correct rectangles instead of a reconstruction.
2. **Rebuild the five rectangles** so they tile the output without
   overlap, using the header scale.  This is the original stripe task,
   now with the exact numbers above.
3. **Workaround now:** shift the preview window down (or crop the top
   of the source) so the overlap is outside the crop.  One state knob,
   costs a little vertical field of view, fully reversible.

## The vendor's full-res message, captured

The earlier harness tree is gone, but our kernel already has the
`cpp_dump_frame` hook (`msm_cpp.c`), so the capture was rebuilt from the
device itself:

1. dump the stock boot partition (`/dev/block/mmcblk0p19`, 23 MB);
2. `bootimg.py unpack`, replace the kernel with our `zImage-dtb`, pack
   with the **ROM's ramdisk** and `cpp_dump_frames=3000`;
3. `fastboot boot` it — the ROM's Android starts on our kernel;
4. `su -c setenforce 0` (the hook's write is denied while enforcing),
   `echo 3000 > /sys/module/msm_cpp/parameters/cpp_dump_frames` (the
   command-line parameter does not reach a built-in module), open the
   camera and take a photo;
5. pull `/data/local/tmp/cpp_frm_*.bin` as root.

The photo produced a **455-word** CPP message, distinct from the 266-word
preview message and accepted by the kernel's checks: `num_stripes` is
`12`, and `130 + 12*27 + 1 = 455`.  It is the **still capture** config,
and its strip coordinates are in the **full-resolution** space (up to
3185 / 2464), unlike the preview message's 960-space.  Saved as
`docs/archive/cppmsg-still-455.bin` (`8ce04d4b3e9385262b8bac1fbfae66b5`).

Fed to our daemon unchanged (`/data/krux/cppmsg.bin`), it is accepted
(`chromatrix sent (455 words, status 0)`, `queue full: 0`) but the
picture is garbage: the still message makes the CPP **process** the
frame for the still capture, not pass the raw through, and our daemon
reads the result as raw luma.

What it gives us is the correct **tiling**: twelve strips that cover the
full-resolution frame without the preview message's five-strip overlap.
The next experiment is the hybrid: the preview message's header and
processing with the still message's twelve tiling strips, which should
keep the raw pass-through and remove the overlap.

### The still message is not a drop-in

Two tests, both restored afterwards:

- unchanged: accepted (`chromatrix sent (455 words, status 0)`,
  `queue full: 0`), but the picture is vertical striations — the CPP is
  processing the still, not passing the raw through;
- with bit 15 of word 5 cleared (`0x01008002` -> `0x01000002`, the only
  pass-through-looking difference from the preview header): no change.

So the still message is the vendor's **processed still path**, not a
full-resolution version of the preview path.  What it gives us is the
correct **tiling** (twelve strips, full-resolution coordinates) and the
full-resolution scale groups.  The next step is to rebuild the preview
message's five strips in its own 960-space using that tiling as the
reference — the original stripe task, now with a known-good tiling to
compare against.

### Message-level attempts, and why they stop here

Three hybrids were tried through `/data/krux/cppmsg.bin`, each restored
afterwards:

| variant | result |
|---|---|
| still message, unchanged | processed still output; vertical striations |
| still word5 bit 15 cleared | same |
| preview header + still 12 strips | dark, almost nothing written |
| preview strips + still scale groups | coherent, QR decodes at 1080, but **zoomed** and the bands **remain and flicker** |

The last one is informative: the still's scale groups shift the output
scale (hence the zoom) but do not change the **strip layout**, so the
overlap is untouched.  The preview message's five strips are the
vendor's layout for a **1280x960 input**; our input is the sensor's
**3280x2464 raw**, 2.56x larger.  No rearrangement of the message can
make a 960-space strip layout tile a 3280x2464 input correctly.

That points at the **input**, not the message: the vendor's preview
path feeds the CPP the ISP's **processed 1280x960** frame, and its
message is exact for that.  Our daemon feeds CAMIF_RAW at full
resolution.  The two ways out are:

1. **PIX_VIEWFINDER at 1280x960** (the vendor's architecture): the ISP's
   own processed output into the CPP.  The earlier `stream=1` test
   delivered no frames and the reason was never found; that is the
   debug to do.  If it delivers, the vendor message is exact and the
   overlap disappears by construction.
2. **The still's processed path**: read the CPP's still output at its
   own size and stride (the vendor's 8 MP processed image).  The still
   message is accepted, but the daemon reads it as 3280x2464 raw; the
   size/stride mismatch is what the striations are.  A daemon-side
   change, larger than a message edit.

## PIX_VIEWFINDER: the vendor preview path, blocked at the VFE output

The next attempt was the vendor's own preview architecture: feed the CCP
the ISP's **processed** frame from `PIX_VIEWFINDER` instead of CAMIF_RAW,
so the captured 1280x960 preview message applies exactly and the CPP (and
its overlapping strips) is bypassed entirely.

### What was set up

- `stream=1` (PIX_VIEWFINDER), `capture_cfg=0` (the capture config
  overwrites the preview's `0x638=7` / `0x444=0x403f`), and the daemon
  built with **`cam_cycle_preview.h`** (the vendor's 7-upload preview
  cycle, `0x444=0x6020`) instead of the 6-upload capture cycle.
- The vendor's initial config (`cam_config.h`, 56 uploads, `0x638=7`) is
  already sent after the stream start.
- Plane sizes tried: the sensor's 3280x2464, 1280x960, and 1640x1232
  (the preview SFE value `0x6020` has a `0x20` scale field, 32/64 = half,
  so 1640x1232 was the best guess).
- A `CONFIG_DONE` (register update) after the vendor config and after the
  per-frame preview cycle — no change.

### What the device says

Instrumented kernel (`tools/vfe-instrument.py`, built by
`scripts/cam/build-vfe-kernel.sh`):

```
VFE40_DBG: hw_version=0x1001001a        (8974 V2)
VFE40_DBG: vfe_clk_src=266670000
VFE40_DBG: camss_csi_vfe_clk=266670000
ISPIF_DBG: INPUT_SEL vfe=0 intftype=0 csid=0 val=0x0
```

So the clock, the hardware version and the ISP-IF routing are right.
`REQUEST_STREAM` succeeds, the stream starts, and then:

- `VFE0 pix0 overflow` 1-4 at the start (the input FIFO);
- `msm_vfe40_process_error_status: image master 0 bus overflow` — the
  VFE's output write master (WM0) overflows;
- **no completed frame** (`GOT A FRAME: 0`, `no frame after 2s`).

There is no `WM_IRQ_MASK` print because a two-plane NV12 stream uses the
**composite** mask (`cfg_comp_mask`), not the per-WM mask — and that
composite path is exactly what CAMIF_RAW uses successfully, so it is not
the fault.

### Where it is blocked

The VFE accepts the input and the stream, but its **output stage** (the
WM/UB bus) never completes a frame. The remaining suspects, in order:

1. **The SFE scale / output size.** `0x444=0x6020` (preview) vs
   `0x403f` (capture); the `0x20` looks like a 32/64 half-scale, which
   would make the preview output 1640x1232. The plane size and the SFE
   must agree; with 1640x1232 the bus overflow count rose to 136.
2. **The AXI UB / bus bandwidth** (`msm_vfe40_cfg_axi_ub`,
   `msm_isp_update_stream_bandwidth`): the "image master 0 bus overflow"
   is the write master's buffer/bus, and the single-stream bandwidth
   formula drops the output term.
3. **The CBCR plane.** The stream is NV12 with two planes and `y_only`
   only narrows the composite mask; the chroma WM still writes, and the
   half-height chroma is the known IOMMU/ping-pong sore point
   (`docs/cam-bringup-vfe.md`, 2026-10-30).
4. **A single-plane stream.** A one-plane request would take the
   `cfg_wm_irq_mask` path (Fix D) and avoid the chroma entirely; the
   CAF's create-stream code picks the plane count from the output
   format, so this needs a Y-only format request.

### State

Restored to the working state: clean kernel `cf9fd9c2edf`, `camd`
`3c6eea90`, `stream=0`, `capture_cfg=1`, CAMIF_RAW + CPP, frames flowing,
`queue full: 0`. The instrumented kernel is at `/tmp/opencode/boot-vfe.img`
and the PIX test daemon at `/tmp/opencode/camd.c.orig` / the build recipe
in this section, for the next session.

## The vendor's VFE stream request, captured (2026-11-11)

Step 1 of the PIX plan: probe the kernel's stream/input/CFG handlers and
run the **vendor camera stack on our kernel** (the ROM's Android, the
boot image from the stock boot partition).  The probe (a printk in
`msm_isp_request_axi_stream`, `msm_isp_cfg_axi_stream` and
`msm_isp_cfg_input`) produced, raw, in
`docs/archive/vfe-request-2026-11-11.txt`:

### The preview stream (the one we want)

```
CAP_REQ:   fmt=0x3132564e (NV21) src=1 (PIX_VIEWFINDER) y_only=0
           drop=1 skip=0 burst=0 hfr=0 fbase=0 divert=1
CAP_PLANE0: w=1280 h=960 stride=1280 lines=960  fmt=0 (Y)      off=0
CAP_PLANE1: w=1280 h=480 stride=1280 lines=512  fmt=3 (CRCB)   off=0
CAP_PLANE2: unused
```

### The still stream (8 MP, via the encoder interface)

```
CAP_REQ:   fmt=0x3132564e (NV21) src=0 (PIX_ENCODER) y_only=0 drop=0 divert=0
CAP_PLANE0: w=3264 h=2448 stride=3264 lines=2496 fmt=0
CAP_PLANE1: w=3264 h=1224 stride=3264 lines=1280 fmt=3
```

### The CAMIF/input

```
CAP_INPUT: src=0 clk=260000000 mux=0 pattern=0 camif_input=3
           lines=2464 pixels=3280 fp=0 lp=3279 fl=0 ll=2463 e0=0 e1=0
```

`CAP_CFG: num=1 cmd=1` (START) for both streams.

## What it means

1. **The vendor preview runs PIX_VIEWFINDER → CPP.**  `divert=1` on the
   preview stream hands the ISP's buffer to the CCP.  That is why the
   CPP message we captured is a **1280x960** message: its input is the
   processed PIX frame, not the raw.  Our CAMIF_RAW+CPP pipeline fed it
   the **3280x2464 raw** — a 2.56x mismatch — and that is the band.
2. Our daemon asked for the stream differently: **NV12** not NV21,
   **y_only=1** not 0, **drop=0** not 1, the **CBCR** plane format (4)
   not CRCB (3), the chroma scan lines 480 not 512, and **no divert**
   for the PIX (the divest block was gated on CAMIF_RAW).
3. Replicating the vendor's request (`stream=1`) made the VFE accept the
   stream and **the CPP process the PIX frames** (193 `cpp_notify_frame_done`
   in ten seconds) — with the NV21/plane geometry the
   `image master 0 bus overflow` disappeared.

## What remains

The daemon still gets no DQBUF on that stream, even though the CPP is
processing.  The remaining work is the **output path** (the CPP's output
buffer → the VFE's vb2 completion: `msm_isp_process_done_buf`,
`processed_divert`, the buffer-manager/vb2 pairing) and the residual
`VFE0 pix0 overflow` (5 at start; the SFE `0x444`/`0x638`/the `0x400`
preview markers are the next comparison).  The daemon must also read the
CPP's output as the **1280x960** the vendor's message produces, and skip
the Bayer demosaic (the PIX/CPP output is already luma).

The working state is restored: `camd 3c6eea90`, `stream=0`,
`capture_cfg=1`, CAMIF_RAW + CPP, frames flowing, `queue full: 0`.

## The PIX path works (2026-11-11)

The remaining blocker from the request capture -- "the CPP processes the
PIX frames but no DQBUF arrives" -- was two separate things, and neither
was the buffer plumbing.

### 1. The CPP output has its own geometry

The daemon's S_FMT and its 8 buffers are sized for the sensor's
**3280x2464** allocation, but the PIX/CPP output is a **packed 1280x960
luma block at offset 0**, written with a **1280-byte stride**.  The
daemon read it with the allocation's 3280-byte stride, so the
1,228,800-byte picture was re-wrapped across exactly
`1280*960/3280 = 374` buffer rows: the "band" at the top of the
`fov=0` 2160x2160 crop (374 buffer rows = 222 crop rows, which is what
`scan_full.raw` showed).  The content began at byte 499,120 =
`32*3280 + 560` -- the crop's own offset -- and the reconstructed block
was a coherent image.  That arithmetic identified the layout without
any vendor-side capture.

The fix is a `read_stride` (1280 on PIX, `cam->width` on the raw paths),
a PIX window of 1280x960 at (0,0), and no Bayer demosaic: the output is
already luma.  Rotated 90 degrees CCW the frame is exactly **960x1280**,
1:1, with no resampling -- the same portrait size as `fov=1` and the
decoder's `K_QUIRC_MAX_IMAGE_DIM`.

### 2. The implementation details the vendor request implies

- PIX needs all three CPP gates (the `isp_start` init, the
  `PRIV_STREAM_ON` firmware load, and the per-frame chromatrix); the
  earlier `isp_stream_src == CAMIF_RAW` guards were removed.
- The per-frame register update (`CONFIG_DONE`) was inside the divert
  block; the PIX path needs it too, both after the initial
  configuration and after each `isp_replay`.
- `epoch_line1 = 0` on PIX, as captured from the vendor.
- The preview message (`cam_chromatrix.h`, 266 words) is the one the
  vendor's preview sends, and it is written for this 1280x960 input.

### 3. Focus: the AF gate was too high for a dim room

The sweep only ran when the published frame's mean was at least 20.
The processed PIX path is darker than the raw one (and the finder is
used in a dim room), so the mean sat at 17-19 and the lens never moved
from the default step 7 -- the QR looked soft even though the geometry
was right.  The gate is now 10; the sharpness score is divided by the
mean, so dim frames compare fairly.  On this scene the sweep chose
step 24, where the coarse score was 4.5x the other steps.

### Baked as the default

- `stream` defaults to 1 (PIX_VIEWFINDER).
- PIX forces the full sensor mode: with the binned 1640x1232 input the
  CPP rejects the message (`msm_cpp_cfg: cannot send frame to
  hardware`, then its processing queue fills) and the stream wedges
  until a reboot.  The vendor's request is a full-sensor one.
- `cycle` defaults to 1 (the per-frame vendor cycle), which is what the
  full-resolution mode needs.

### Verified on the device

- Full-frame preview: clean, no stripes, no band.
- QR: `Tools -> Datum Tool -> Via Camera` decodes
  `UR:crypto-psbt`, binary **447 bytes PSBT**.
- Entropy: `New Mnemonic -> Via Camera -> 12 words -> Proceed`
  reports **"Good entropy"**, Shannon **5.2 bits/px** (threshold 3),
  pixel deviation **38** (threshold 10).
- Steady state after a clean boot: `pix0 overflow` 0,
  `process queue full` 0, no `DQBUF` failures.

The working state is the project tree: `camd 99ef6da3`, state
`stream=1 mode=1 cycle=1 capture_cfg=0`.  The device runs the tree
kernel `cf9fd9c2edf` (`fastboot boot /tmp/boot-dev.img`); nothing was
flashed, and the QR display phone (the LG) was read from its screen
only.
