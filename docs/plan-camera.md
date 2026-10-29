# Plan — camera on the hammerhead

Status 2026-10-27. This is the ordered plan to make the IMX179 feed Krux's
`camera`/`sensor` pages on the air-gapped Nexus 5. It assumes the existing
tree: `linux` at `airgap/master-cleanup` (`95f4b0f38ce`), the port on
`krux/nexus-port`, and the imaging server in `cam/imaging8.c`.

Two facts change the earlier "the kernel is broken" framing:

1. The ROM's camera produced real JPEGs under **our** kernel with the
   vendor HAL (2026-10-21/22 runs). The kernel camera stack can work.
   Until a kernel difference is measured, Phase A is a userspace
   sequencing/geometry problem.
2. `bytesused=0` in the logs is `v4l2_buffer.bytesused` read on an
   `V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE` buffer. The correct field is
   `buf.m.planes[i].bytesused`; these drivers never set it. The only
   hard evidence of emptiness is the zero-byte scan, and that scan covers
   only `width*height` of the Y fd. Re-measure before treating it as proof.

## 1. Goal and definition of done

Deliver one path: cold boot → Krux → camera page → live QVGA preview →
`find_qrcodes()` returns a payload → entropy page captures statistics →
exit page. No modem, Wi-Fi, Bluetooth, vendor HAL, or Android framework.

Done means all of:

- `sensor.reset()` opens the camera and returns; `sensor.get_id()` is `0x179`.
- `sensor.set_pixformat(RGB565|GRAYSCALE)`, `set_framesize(QVGA)`,
  `set_windowing`, `set_hmirror`, `set_vflip`, `skip_frames`, `run`,
  `snapshot`, `close` all work at module level, not only on a class instance.
- `snapshot()` returns an `image.Image` of 320×240, not a stub.
- `find_qrcodes()` decodes a QR code at 15–25 cm; `get_statistics()` returns
  usable `l_stdev`/`a_stdev`/`b_stdev`; `to_bytes()` returns RGB565 bytes.
- The camera page renders through `lcd.display(img)` without crashing.
- 10 minutes / 1,000 snapshots with no oops, reboot, fd leak, or monotonic
  `gc.mem_free()` loss; frame rate ≥ 8 fps at QVGA.
- `ip addr`, `/proc/net/dev`, and `lsmod` show no new non-loopback interface
  or module. No camera daemon runs when no camera page is open.

Out of scope for the first landing: full-resolution stills, VGA, autofocus,
anti-glare control, CAMIF_RAW/CPP colour stills, and video recording.

## 2. Phase A — kernel/V4L2 bring-up

### A0. Freeze the baseline

- Confirm the tree and image:
  `git -C linux log -1 --oneline` → `95f4b0f38ce`.
- Build only to `/tmp/opencode`; never flash. Use `fastboot boot`.
- Current build output `/tmp/opencode/ko-build/arch/arm/boot/zImage-dtb`
  is from an earlier tree. Rebuild before trusting it:

```sh
TOOLCHAIN=host bash tools/build-kernel.sh --out /tmp/opencode/ko-cam
sh rootfs/make-boot-image.sh \
  artifacts/twrp-3.7.0_9-HH.R.17-MADV_WIPEONFORK.img \
  /tmp/opencode/ko-cam/arch/arm/boot/zImage-dtb \
  /tmp/opencode/boot-cam.img
```

For a camera build, devtmpfs must be on for this build only:

```sh
sed -i 's/^CONFIG_DEVTMPFS=n/CONFIG_DEVTMPFS=y/' tools/profiles/hammerhead-keep.list
# ... build ...
sed -i 's/^CONFIG_DEVTMPFS=y/CONFIG_DEVTMPFS=n/' tools/profiles/hammerhead-keep.list
```

Do not pass `PROFILE=tools/profiles/hammerhead-camera.list`: that file is an
enable list consumed by the keep-list loop, not a strip profile.  The default
`PROFILE=tools/profiles/hammerhead.list` is correct; the camera symbols come
back through `hammerhead-camera.list`.

`rootfs/stock/boot-ourkern-clean-twrp.img` no longer exists; the TWRP image
under `artifacts/` is the current valid header/ramdisk template. The kernel
must be `zImage-dtb`, not `zImage`.

- Boot and prove it is ours: `adb get-state` is `recovery`, `id -u` is 0,
  `/proc/version` contains the built commit, and `fastboot` printed
  `Booting OKAY`. If not, stop and repeat; a silent fallback to the boot
  partition invalidates every measurement.

### A1. Verify the device nodes first

```sh
adb shell 'ls -l /dev/video0 /dev/video1 /dev/v4l-subdev11 \
  /dev/v4l-subdev13 /dev/v4l-subdev15 /dev/v4l-subdev17'
adb shell 'cat /proc/filesystems | grep -E "devtmpfs|tmpfs"'
```

`CONFIG_DEVTMPFS=n` in `tools/profiles/hammerhead-keep.list`; the nodes come
from TWRP's ueventd, not the kernel. If any node is missing, fix the stub
ramdisk/ueventd before any camera work. Do not start Phase A debugging with
half the nodes absent.

### A2. Fix the measurement before chasing the symptom

Patch `cam/imaging8.c` (lab tool only):

- print `buf.m.planes[0..1].bytesused`, `.length`, `.data_offset`, and the
  reservation fd for each dequeued buffer;
- map each plane over its **mapped ION length**, not `width*height`, and
  count non-zero bytes plus a 16×16 sample grid;
- save `/data/local/tmp/frame<idx>.nv12` and its Y/UV sizes;
- print `pix_count`/`raw_count` from the VFE log in the same run.

Build and push:

```sh
arm-linux-gnueabihf-gcc -O2 -static -march=armv7-a -mfpu=neon-vfpv3 \
  -mfloat-abi=hard -Icam -o /tmp/opencode/imaging-cam cam/imaging8.c -lpthread
adb push /tmp/opencode/imaging-cam /data/local/tmp/imaging
```

Run the binned, processed path first (source `1` = `PIX_VIEWFINDER`, no CPP):

```sh
adb shell 'cd /data/local/tmp && ./imaging \
  1640 1232 0x40000000 rear 0 - 1 binned cfg nocycle \
  0 0x2b 0 0 300 0 0 0 0x12 0 1 0xffffffff 0 0 0 1 0'
```

Argument order is `main()` in `cam/imaging8.c`; do not reuse
`scripts/cam/imaging.sh` until its comments (which call arg 27 the source)
are corrected. For this test, `cpp_fw_path` is `-`; patch `cpp_load_firmware`
and `cpp_send_chromatrix` to skip when it is `-` or the source is not
`CAMIF_RAW`. The CPP is not in the processed preview path and its photo-mode
chromatrix can only confuse the result.

Acceptance for A2: either `planes[0].bytesused > 0` and Y non-zero, or a
full-length scan that is genuinely all zero. Save both numbers in the log.
If pixels are present, go straight to Phase B; Phase A is finished.

### A2a. Result of the 2026-10-28 run (status)

The binned `PIX_VIEWFINDER` path was run with the measurement patch.  Result:
no frame completes; the client never gets a `DQBUF` and the kernel logs
`msm_ispif_read_irq_status: VFE0 pix0 overflow.` (~710 lines in 8 s).  The
sensor is programmed correctly (`0x034c..0x034f = 1640/1232`).  This is a
kernel/VFE drain problem, not a buffer-geometry problem, so the processed
path does not deliver a frame.

The `CAMIF_RAW` path (CPP firmware v1.2.0) was then run per §A3 and **does**
deliver a frame: `GOT A FRAME: index=2`, the dequeued buffer holds ~900 k
non-zero bytes, and `/data/local/tmp/frame0.nv12` (2 020 480 bytes) was saved.
Stage 2 acceptance is met on the raw path; the processed path remains deferred
until the VFE overflow is explained.  Phase B (`camd`) should use `CAMIF_RAW`
for the first landing.

### A3. Test the three stream sources

Run each with the same binned geometry and no CPP:

| source | value | expected |
|---|---|---|
| `PIX_VIEWFINDER` | 1 | preview path; vendor preview uses this |
| `PIX_ENCODER` | 0 | encode path; same pixel front end |
| `CAMIF_RAW` | 2 | raw path; only this one needs the CPP |

For `CAMIF_RAW`, pass the CPP firmware
`/lib/firmware/cpp_firmware_v1_2_0.fw`, `cam_capture.h`, `cam_cycle.h`, and
`capture=1`; for the processed paths, no CPP and `capture=0`.

Record for each: `pix_count`, `raw_count`, `GOT A FRAME`, plane bytes,
`camif error status`, CSID mapped/unmapped long packets, and fps.

Decision gate: if `PIX_VIEWFINDER` delivers pixels, freeze it for Krux and
stop debugging `CAMIF_RAW`. The raw/CPP path is a still-photo feature, not a
QR-scan requirement. If neither processed path delivers pixels, continue to
A4/A5; do not start Phase B.

### A4. Rebuild the `buf_done` instrumentation (b25)

The b25 build is not in the current tree and must not be assumed. Use a
throwaway worktree so `airgap/master-cleanup` stays clean:

```sh
git -C linux worktree add /tmp/opencode/linux-cam-debug \
  -b airgap/cam-debug 95f4b0f38ce
```

Instrument (ratelimited `pr_info`, prefix `CAMDBG`):

- `msm_isp_process_done_buf()` in `isp/msm_isp_axi_util.c`: stream handle,
  stream source, output format, plane count, buf index, bufq handle, first
  mapped paddr, and whether `buf`/`ts` are NULL.
- `msm_isp_buf_done()` in `isp/msm_buf_mgr.c`: bufq session/stream, buf index,
  state before/after, `vb2_buf` index and `v4l2_buf.sequence`.
- `msm_isp_request_axi_stream()`: source, format, planes, WM indices, plane
  width/height/stride.
- `msm_vfe40_cfg_io_format()`: print `io_format_reg` before/after at `0x54`.
- `msm_vfe40_cfg_camif()`: print `0x1c`, `0x2e8`, `0x2f4`, `0x300`, `0x304`,
  `0x308`, `0x314` as written.
- On every 100th EOF/error interrupt: dump `0x28`, `0x30`, `0x40`, `0x4c`,
  `0x54`, `0x70..0xa8`, each active `VFE40_WM_BASE(wm)+0x0/0x14/0x18/0x1c/0x20`,
  and `VFE40_XBAR_BASE(wm)` (base `0x6c + 0x24*wm`, xbar `0x58 + 4*(wm/2)`,
  shift 0 or 16).

Build from that worktree with `--tree /tmp/opencode/linux-cam-debug` and
boot it. Run A2 again and collect:

```sh
adb shell 'dmesg | grep -a CAMDBG' > /tmp/opencode/camdbg-run.txt
```

Interpretation:

- no `process_done_buf`: the frame-done interrupt does not reach the buffer
  completion path (stream start/update or WM IRQ mask).
- `process_done_buf` but no `buf_done`: state/divert/bufq lookup.
- `buf_done` with a valid vb2 index and zero bytes: WM address, XBAR, or
  output geometry, not the buffer manager.
- `pix_count=0` while CAMIF is enabled: input source, ISP-IF routing, or
  CAMIF timing.
- `camif error status 0xd0`: MIPI fatal A+B; re-check CSID LUT/VC and the
  data type. `0xd0` is not a buffer error.

### A5. Compare against the vendor preview, in our kernel

The decisive comparison is the vendor HAL running under **our** instrumented
kernel, not the ROM's kernel. Pack our kernel with the ROM Android ramdisk
from `artifacts/boot-kernelsu-20261016.img`, boot it, and use the vendor
camera app:

```sh
adb shell su -c 'input keyevent 224'
adb shell su -c 'am start -a android.media.action.STILL_IMAGE_CAMERA'
sleep 12
adb shell su -c 'dmesg | grep -a CAMDBG' > /tmp/opencode/vfe-vendor.txt
adb shell su -c 'am force-stop com.google.android.GoogleCamera'
```

Take the vendor's preview register state (CAMIF block, `0x54`, `0x174..0x1fc`,
`0x444`, `0x464`, `0x638`, WM/XBAR) and diff it against ours from A4. Known
vendor preview markers from earlier captures: CAMIF block reads zero,
`0x444=0x6020`, `0x464=0x18000fc`, `0x638=7`. The capture path markers are
`0x400=0x10000`, `0x444=0x403f`, `0x464=0x30001f5`, `0x518=0x9380da`,
`0x638=0`, `0x37c=1`. Do not apply capture markers to a preview stream.

If the registers match and `pix_count` still differs, the difference is in
the ioctl sequence/timing, not a register value; log the ioctl order next.
Stop after three comparison cycles without a new measurement; record the
blocker rather than sweeping values.

### A6. Geometry and mode rules

- Use the binned IMX179 mode for Krux: HMAX/VMAX `1640×1232`, active
  `1632×1224`, CAMIF `1640×1232`, pixel clock `60.6 MHz`.
- CAMIF window is full line/frame: `0x300 = 1232<<16 | 1640`,
  `0x304 = 0<<16 | 1639`, `0x308 = 0<<16 | 1231`, `0x314 = 0xffffffff`,
  `0x2e8 |= 3`.
- The VFE40 pixel path has no general downscaler. Asking for a 320×240 WM
  against a 1640×1232 CAMIF crops; it does not scale. Do not judge QVGA
  quality from that. Phase B owns the downscale.
- Keep `crop_enable=1` on the ISP-IF; the PIX0 enable bit depends on it.

## 3. Phase B — MaixPy-compatible sensor shim

### B1. Architecture decision

Use a small camera daemon, `cam/camd`, derived from `cam/imaging8.c`. The
daemon owns `/dev/video0`, `/dev/video1`, the sensor, CSID/PHY, ISP-IF, ISP,
and (only for raw) CPP, then delivers QVGA frames over a Unix socket.
`krux/src/sensor.py` is a thin MaixPy API over that socket.

Why: the camera2 state machine is a privileged userspace server that must
answer events; putting it inside MicroPython couples a camera oops to the
wallet. A daemon can be restarted and keeps the Python heap small. Do not
duplicate `imaging8.c` in a C extension.

### B2. Daemon behaviour

- Start with the mode chosen in Phase A (default `PIX_VIEWFINDER`, binned).
- Configure the IMX179 from `sensor_live.h`/`sensor_config.h` and the
  per-frame sequence from `cam_cycle.h` only for the matching path.
- Allocate four ION buffers with heap id 30 (`1 << 30`), map, `QBUF`, and
  stream. Handle the camera2 config events on `/dev/video0` in a thread.
- For each `DQBUF`, convert the full 1640×1232 NV12 frame to QVGA once, in C:
  centre-crop to 1600×1200 and 5×5 box-average to exactly 320×240. For
  `GRAYSCALE`, produce Y only; for `RGB565`, produce Y + 160×120 UV.
- Keep a latest-frame slot per client and drop old frames. Never block the
  camera loop on a slow client. Target 10–15 fps; report actual fps.
- On `STREAM_OFF`, stop the sensor, stream off, and free buffers; on exit,
  release CSID/ISP and close nodes. The old close-path panic is fixed; verify
  it with repeated start/stop.

### B3. Control and frame protocol

Unix `SOCK_SEQPACKET` at `/data/krux/camd.sock`, little-endian fields:

- request: one byte command (`H`ello, `S`tart, `T`op, `R`un, `F`rame,
  `W`rite-reg, `Q`uit) plus fixed arguments;
- `F` reply: 32-byte header `KCF1`, seq, src_w, src_h, out_w, out_h,
  stride_y, stride_uv, y_bytes, uv_bytes, timestamp, status, then Y and UV;
- `W` arguments: 16-bit register, 8-bit value; accepted only while stopped;
- `S` arguments: mode, source, output format, crop rect; returns status.

`sensor.snapshot()` sends `F`; a timeout of 2000 ms raises `SensorError`.
`skip_frames(n)` sends `F` and discards `n` replies. `run(0)` sends `T` but
keeps the daemon alive; `close()` sends `Q`.

### B4. `sensor.py` API

Replace the stub with a module-level singleton. The current module-level
`reset()`/`run()`/`snapshot()` each construct a new `sensor()`, so format and
size settings never persist; that is a bug, not a design.

| function | semantics |
|---|---|
| `reset(freq=18200000, dual_buff=False, choice=None)` | start/connect daemon, configure binned mode, `run(0)`; `freq` stored only |
| `get_id()` | `0x179` |
| `set_pixformat(f)` | `GRAYSCALE` or `RGB565`; other values raise `ValueError` |
| `set_framesize(f)` | `QVGA` → 320×240; `VGA` accepted but mapped to QVGA with a one-time warning (see risks) |
| `set_windowing(roi)` | validate `(x,y,w,h)`; apply as a software crop in `snapshot()` |
| `set_hmirror(v)` / `set_vflip(v)` | software transform in `snapshot()`; no sensor write |
| `skip_frames(num=10, time=0)` | discard `num` frames or until `time` ms |
| `run(enable)` | idempotent stream start/stop |
| `snapshot()` | return the current `Image`; never return `None` |
| `close()` | stop daemon if this process started it |
| `__write_reg(r,v)` and `_Camera__write_reg(r,v)` | send `W`; both names must exist because `sensor.__write_reg` inside `Camera` mangles to `_Camera__write_reg` |

Module-level functions forward to the singleton. Keep the existing constants
(`QVGA`, `VGA`, `RGB565`, `GRAYSCALE`, …). Do not edit upstream `camera.py`
for the first landing: an unknown `cam_id=0x179` falls through every
sensor-specific branch, which is the correct generic behaviour. If a later
page needs a branch, add `IMX179_ID = 0x179` and one comment as a separate
upstreamable patch.

### B5. Heap and buffers

The MicroPython unix port's default GC heap is 1 MB. Do not allocate the
1640×1232 frame or a 3 MB Python `bytes`. The daemon sends at most
320×240×1.5 = 115 KB. `_image` allocates its pixel buffer with `malloc`,
owns it for the object's lifetime, and frees it in the finalizer; expose
`from_nv12(...)` so `snapshot()` never copies through a Python list.

### B6. Build and deploy

- Add `kruxport/image_maixpy.c` (native `_image`) and
  `kruxport/camhost_linux.c` (`start`/`stop` via `fork`+`execv`, `waitpid`);
  register both in `kruxport/apply-micropython-port.sh` and
  `ports/unix/Makefile`.
- Build the daemon and firmware:

```sh
arm-linux-gnueabihf-gcc -O2 -static -march=armv7-a -mfpu=neon-vfpv3 \
  -mfloat-abi=hard -Icam -o /tmp/opencode/camd cam/camd.c -lpthread
bash scripts/build/build-micropython.sh
```

- Extend `scripts/deploy-firmware.sh` to push `/data/krux/camd` and
  `/data/krux/camd.sock`'s parent, then `DEPLOY_RESTART=1`.
- Keep the port modules in one place: `kruxport/kruxport/` is the source,
  and `krux/src/` is what `deploy-firmware.sh` pushes. Sync both in the same
  commit, or change deploy to push `kruxport/kruxport/`; do not let them
  diverge.

## 4. Phase C — frame path in Krux

### C1. Native `image` module

Implement the subset the firmware actually calls, as `_image` plus a thin
`image.py` re-export. All pixel work is C; Python only forwards.

- `Image(size=(w,h), copy_to_fb=False)`, `width()`, `height()`, `clear()`,
  `set_pixel((x,y),c)`, `get_pixel(x,y)` (accept both tuple and two ints).
- `draw_rectangle(rect_or_xywh, color, fill=False, thickness=1)`,
  `draw_line`, `draw_string` (8×16 VGA font), `draw_image(img,x,y,x_scale,y_scale)`,
  `binary([(lo,hi)])`, `to_grayscale()`, `lens_corr(...)` (no-op),
  `to_bytes()` (RGB565 little-endian), `save(path)` (24-bit BMP).
- `from_nv12(data, src_w, src_h, stride_y, stride_uv, dst_w, dst_h,
  gray=False, crop=None, hmirror=False, vflip=False)`.
- `get_statistics(roi=None)` → object with `l_mean/l_median/l_stdev`,
  `a_*`, `b_*`, and `median()`; approximate CIELAB from RGB565, exact
  colorimetry is not required for entropy variance.
- `get_histogram(roi=None).get_threshold().value()` → Otsu threshold over
  the L channel; `find_blobs([(lo,hi)], x_stride=, y_stride=,
  area_threshold=)` → objects with `rect()`, `x()`, `y()`, `w()`, `h()`.
  These keep `tiny_seed` working after the stub is replaced.

### C2. QR decoding

Vendor `quirc` under `kruxport/quirc/` (ISC licence, ~2 files) and call it
from `_image`:

- convert the Image to 8-bit grayscale (Y plane if the snapshot is gray);
- `quirc_new`/`quirc_resize` once, decode normal and inverted images when
  `find_inverted=True`; merge and de-duplicate by payload and quad;
- return a list of objects with `payload()` (str), `x()`, `y()`, `w()`,
  `h()`, `corners()`, `rect()`.

Reject false positives shorter than 4 characters only at the page level, not
in the decoder. `qr_capture.py` already passes `find_inverted=first_frame`.

### C3. Display

`kruxport/lcd_bcm.c` currently takes `bytes` or an integer address. Extend
`lcd_display()` to accept `_image.Image` by checking the shared type (include
`image_maixpy.h`), walking RGB565 pixels to the panel's 32-bit format. This
is required for `display.render_image()`, `flash_tools`, and `tiny_seed`,
all of which call `lcd.display(img, oft=..., roi=...)` with an Image object.
Keep the integer-address path for compatibility.

### C4. Krux integration

No upstream edit is needed for the first landing. The chain is already:
`Camera.__init__` → `sensor.reset/get_id/set_pixformat/set_framesize` →
`Camera.snapshot()` → `qr_capture` → `img.find_qrcodes()` →
`display.render_image(img)`. The only firmware change is the port modules.
If `camera.py` must learn the IMX179, add only the constant and a comment;
do not fork page logic.

## 5. Phase D — validation and hardening

### D1. Host tests

- Unit-test `_image` NV12→RGB565/grayscale, crop, mirror/flip, `to_bytes`,
  statistics, Otsu, and `save` round-trip.
- Build a QR with the existing `qrcodegen` and assert `find_qrcodes()` returns
  the exact payload, including a white-on-black image for `find_inverted`.
- Run the same tests under the arm32 build (qemu or device) before flashing.

### D2. Device smoke

```sh
DEPLOY_RESTART=1 bash scripts/deploy-firmware.sh
adb shell 'cd /data/krux && ./micropython -c "
import sensor, image
sensor.reset(); print(hex(sensor.get_id()))
sensor.set_pixformat(sensor.RGB565); sensor.set_framesize(sensor.QVGA)
sensor.run(1)
for i in range(10):
    img = sensor.snapshot(); print(i, img.width(), img.height())
sensor.run(0)"'
```

Pass: ten distinct frames, 320×240, no `SensorError`, daemon logs 10–15 fps,
`dmesg` clean.

### D3. Page end-to-end

- QR capture page: 20 scans of the same code, 20/20 payloads; one inverted
  frame.
- Entropy page: `get_statistics()` moves with scene variance; `shannon`
  consumes `to_bytes()`; capture completes.
- Fill-flash, QR view, tiny seed, flash tools: no `AttributeError`, no crash,
  no blank screen.

### D4. Stability and leaks

- 1,000 snapshots in a loop; `gc.mem_free()` before/after within 8 KB.
- 30 minutes on the QR page; no oops, no reboot, no fd growth
  (`ls /proc/<pid>/fd | wc -l` stable).
- 50 `reset`/`run(0)`/`run(1)`/`close` cycles; no `camif` error, no CSID
  close panic.
- Battery temperature and frame rate after 10 minutes: ≥ 8 fps, no thermal
  shutdown.

### D5. Failure and air-gap checks

- Kill `camd` while the page is open: `snapshot()` raises `SensorError`;
  the page reports "no camera" instead of crashing; restart recovers.
- Boot without `/data/krux/camd`: camera pages show the fallback, wallet
  pages still work.
- After a camera session: no new non-loopback interface, no listening
  socket except the Unix socket, no module loaded, `camd` not running.

## 6. Risks and blockers

- **No VFE scaler.** QVGA from the binned frame needs the daemon's 5×5 box
  downscale. If 1640×1232 streaming is too much bandwidth, fall back to a
  centred VFE crop (narrower FOV) or 2×2 to 800×600; measure before choosing.
- **Processed vs raw path (updated 2026-10-29).** `PIX_VIEWFINDER` is blocked
  by `VFE0 pix0 overflow` (no frame completes). `CAMIF_RAW` + CPP v1.2.0
  delivers non-empty frames and is the path used by `camd`. Prefer `CAMIF_RAW`
  for the first landing. Revisit `PIX_VIEWFINDER` only after the VFE overflow
  is explained via A4/A5 register comparison.
- **Sensor mode.** The photo sequence and the binned sequence differ. Writing
  the live photo list over a binned configuration returns the sensor to
  3280×2464. Use mode 0 (`IMX179_MODE_TABLE` + `imx179_settings_7`) for
  binned and verify `0x034c..0x034f` read back as 1640/1232.
- **`fastboot boot` fallback.** Always verify `/proc/version`, uid, and adb
  state. A fallback run measures the wrong kernel.
- **Device nodes.** `CONFIG_DEVTMPFS=n`; TWRP ueventd supplies the camera
  nodes. A ramdisk change can silently remove them.
- **MicroPython heap.** 1 MB default. Large frames must stay in native
  `malloc` buffers, never `bytes`/`bytearray` on the GC heap.
- **Image API drift.** Replacing the no-op stub can break `tiny_seed`'s
  histogram/blobs and `qr_view`'s `draw_image`/`save`. The compatibility
  subset in C1 is part of the definition of done.
- **Quirc performance.** On a Cortex-A9, QVGA decode may be 20–50 ms. Target
  5–10 fps scanning, not 30; drop frames rather than queue them.
- **Upstream merge cost.** Keep all port code under `kruxport/` and the
  `nexus-port` branch. Any `krux/src/krux` edit must be a minimal,
  upstreamable patch with a test.
- **Privacy.** `camd` is started only from `sensor.reset()` and stopped by
  `close()`; no background capture, no persistent socket after the page
  exits.

## 7. Test matrix

| # | test | command / action | pass signal | on fail |
|---|---|---|---|---|
| 1 | kernel identity | `fastboot boot /tmp/opencode/boot-cam.img` | `Booting OKAY`; `/proc/version` has built commit; uid 0 | repeat; do not measure |
| 2 | nodes | `ls /dev/video* /dev/v4l-subdev*` | video0/1, subdev11/13/15/17 present | fix ramdisk/ueventd |
| 3 | sensor binned | run A2; `sensordump` | `0x034c..0x034f` = 1640/1232 | fix mode sequence |
| 4 | processed frame | A2, source 1 | `planes[0].bytesused>0`, Y non-zero, `pix_count>0` | A4 instrumentation |
| 5 | raw frame | A3, source 2 + CPP | same, `camif error` not `0xd0` | keep raw deferred |
| 6 | buf_done | A4, `dmesg | grep CAMDBG` | `process_done_buf` → `buf_done` with vb2 index | locate stop point |
| 7 | vendor diff | A5, vendor app under our kernel | register diff explained | record blocker after 3 cycles |
| 8 | shim API | D2 script | all calls return; 10 frames 320×240 | fix sensor.py |
| 9 | QR | QR page, code at 15–25 cm | payload appears; `find_inverted` works | tune crop/exposure |
| 10 | entropy | entropy page | `l/a/b_stdev` non-zero and scene-dependent | fix statistics |
| 11 | display | QR page / flash tools | live image and progress bar render | fix lcd Image path |
| 12 | stability | D4 loops | no oops/reboot/fd leak; ≥8 fps | stop and profile |
| 13 | air-gap | D5 | no new interface/socket/module | remove/stop daemon |

> **Path note:** this document predates the source consolidation. The port
> modules are now in `krux/src/` and `micropython/ports/unix/`; `kruxport/`
> no longer exists.
