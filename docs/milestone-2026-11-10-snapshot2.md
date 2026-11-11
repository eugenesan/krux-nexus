# Second working snapshot: full-resolution camera (2026-11-10)

This is the point to return to after the full-resolution work.  It
supersedes `milestone-2026-11-10.md` (the binned snapshot): the camera
now runs the sensor's full 3280x2464 mode, and the preview, the QR and
the entropy all work from it.  What is still visibly wrong is the
preview's right-hand stripes, and they are **display-only** (see below).

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
