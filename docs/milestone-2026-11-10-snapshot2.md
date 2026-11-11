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
