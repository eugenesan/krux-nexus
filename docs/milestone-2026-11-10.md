# Milestone: the camera works (2026-11-10)

The point to return to.  The camera preview is live, fast, stable and
band-free apart from one narrow vertical strip, and the QR decodes
quickly.  This file records exactly what that state is made of.

## The commits

| tree | commit | what it is |
|---|---|---|
| `krux-nexus` (main) | `d06c6bb` | the docs and the submodule pointers |
| `linux` | `cf9fd9c2edf` | the camera_v2 fixes (see below) |
| `krux` | `2ff5a725` | the Krux port: the auto-exposure, the display |
| `krux/firmware/micropython` | `4827d223a` | the unix port: the preview drawing |

To come back here after further work:

```
cd /home/user/Develop/src/krux-nexus
git checkout d06c6bb
git submodule update --init --recursive
```

## The artifacts, and how they are built

| artifact | built by | md5 |
|---|---|---|
| `cam/camd` | `bash scripts/cam/build-camd.sh cam/camd` | `9e4f42c7bb2ddb37c05ae1e72153add7` |
| `rootfs/micropython-arm32` | `bash scripts/build/build-micropython.sh` | `2e79e2598fad5dd12a990c1fb8339cad` |
| the kernel | `bash tools/build-kernel.sh --tree linux --out /tmp/opencode/ko` | zImage-dtb `d308f907185217b03a8e531504924ac0` |
| the boot image | `bash rootfs/make-boot-image.sh artifacts/twrp-3.7.0_9-HH.R.17-MADV_WIPEONFORK.img /tmp/opencode/ko/arch/arm/boot/zImage-dtb /tmp/boot-dev.img` | `b9b01afc539228574a287848a05e5875` |

Deploy: `ANDROID_SERIAL=0879524b0068abca bash scripts/deploy-firmware.sh`
(that pushes `camd`, the interpreter and the Python and restarts the
firmware).  Never flash: `fastboot boot /tmp/boot-dev.img` only.

## The device's state

```
/data/krux/state/mode          0     the binned 1640x1232 sensor mode
/data/krux/state/stream        0     CAMIF_RAW: the ISP's raw into the CPP
/data/krux/state/capture_cfg   1     the vendor's capture ISP configuration
/data/krux/state/cycle         0     no per-frame ISP cycle (that is for full res)
/data/krux/state/profile       0     the daemon's per-phase timing (1 turns it on)
```

The QR display phone (LG `LGL322DL71cd2c3`) shows the Sparrow UR code in
the Gallery, `svc power stayon true`, brightness around **60-140**: past
that the screen clips the sensor even at the minimum exposure and the QR
page's auto-exposure cannot win against a self-luminous subject.

## What was wrong, and what fixed it

1. **The stall.** The ISP takes a buffer per frame and nothing ever gave
   it back: the shared-buffer accounting wants two puts and the second
   never comes.  The pool drained, the ISP stopped, the picture froze and
   the CPP got no output buffer (`CFG: EAGAIN`).  Fixed by holding the
   written buffer and giving it back two frames later.
2. **The blank frames.** The daemon told the CPP which buffer held the
   frame, and it could not know: the ISP and the CPP take buffers from
   the same pool, so the order the daemon sees is not the ISP's order.
   The kernel now records the buffer the ISP wrote (the divert path) and
   the CPP reads that one.
3. **The frame rate, 7.5 -> 34.5 fps.** The downscale did four integer
   divisions per output pixel and this CPU has no divide instruction.
   The box edges are a table now and the "divide by 1 or 2" are shifts.
4. **The flicker.** The auto-exposure chased a delayed, noisy
   measurement: the step is 25% and the measurement is smoothed.
5. **The AF/AE cycle.** An exposure change used to restart the autofocus
   sweep, so the two chased each other (blink, dark, repeat).  A sweep is
   only started when the user asks for one.

## What is left

- A **narrow vertical strip** (about 7% of the width, on the left) that
  is mostly transparent and flickers with movement: the CPP's five chroma
  stripes.  Their rectangles were drawn for the vendor's 1280x960 preview
  and overlap this mode's 1640x1232 frame, so the last stripe to write
  wins over part of the frame.
- A **single** stripe gives a clean, decodable picture (`UR:CRYPTO-PSBT/`)
  but **cannot be used**: the CPP's firmware needs the stripes to tile the
  frame, and with one its processing queue fills and never completes
  (`process queue full. drop frame`), so the camera stops.
- What the stripe experiments established (each one a
  `/data/krux/cppmsg.bin` and a cold boot):
  - The CPP's firmware **requires the stripes to tile the frame**.  One
    stripe, or five identical ones, fills `process queue full` and the
    camera stops.  A message that keeps the tiling and only changes the
    geometry runs clean (`queue full: 0`), which is the signal to watch.
  - The **scale** lives in the message header: scaling the three groups
    at words 43-46 `(159, 310, 488, 1725)`, 51-54 `(189, 229, 205, 749)`
    and 59-62 `(158, 191, 180, 641)` by 1.28 changes the picture's scale
    (by 0.5 it changes the other way).  Neither removed the overlap.
  - The stripe **rectangle's points** are the packed 16.16 pairs at each
    entry's `+6`, `+8` and `+15`; their maximum is 959, the vendor's
    960-tall preview space.  Scaling those by 1.28 keeps the tiling
    valid (`queue full: 0`) but breaks the horizontal alignment into a
    comb -- so the pair's two halves are not both horizontal/vertical in
    the way they first look, and the *y* half alone is the one that
    needs the 1232/960 factor.
  - The next step is therefore: scale **only the vertical half** of
    `+6/+8/+15` by 1232/960, leave the horizontal half alone, and watch
    `queue full` stay at 0 and the comb disappear.
- The message can be overridden without rebuilding:
  `/data/krux/cppmsg.bin`, a raw little-endian array of 32-bit words.  It
  must start with `0x3e646d63` and end with `0xabcdefaa`.

## Observed on the rebuilt state (to test and fix after the stripes)

Recorded as reported, on the clean rebuild (`gcf9fd9c2edf`, `camd`
`9e4f42c7`, interpreter `2e79e259`):

1. A **7 % strip on the left** of the preview that flickers when
   something moves in the frame.
2. A **20 % band immediately right of that strip**, transparent but
   visible when something moves.
3. The camera **finds and decodes a QR**, then the page throws
   `ValueError('Failed to decode BIP39 from CBOR')`.  The phone is
   showing the Sparrow **PSBT UR**, which belongs on Sign -> PSBT, not
   on Load Mnemonic -> QR Code, so the payload type is the mismatch,
   not the decoder.
4. The **entropy preview started to flicker**: its auto-exposure sits at
   the maximum (`exp=0x09d6`, `gain=0x00ff`) with the picture below its
   target (`ENTROPY_TARGET` 128) and the phone's screen changing under
   it.

Numbers 1 and 2 are the five chroma stripes (see below).  Number 4 is
the auto-exposure being unable to darken a self-luminous screen: the
same reason the QR page cannot expose for a screen that is too bright.

## The rules this state respects

- The tree is the source of truth; the device's copy is a deploy
  artifact, never edited.
- Temporary camera state lives in `/tmp/krux` (tmpfs), never symlinked
  into the tree.
- No modem, Wi-Fi, BT or vendor HAL; the preview stays modest.
- The entropy capture must not be broken by camera tuning.
