# Hardware bring-up: the methods and the tools

What was done to bring the Nexus 5 up, in the order it was done, and
what to reach for first on the next device.  The point is not to repeat
the work: it is to skip the three days of archaeology that the work
needed.

The rules the whole effort followed:

* **never flash**.  Every image is booted with `fastboot boot <image>`,
  so the phone's own partitions are never touched and the device can
  always come back.
* **the project tree is the source of truth**.  What is on the device
  is a deployment artifact; a change is made in the tree and pushed.
* **temporary state in /tmp/krux** (a tmpfs on the device), **persistent
  knobs in /data/krux/state**, and one serial number, one device.

## 1. The order of the work

1. **A picture, not a camera.**  Before touching the camera, the
   display, the touch and the NFC had to work: the display through a
   framebuffer (`/dev/graphics/fb0`, 1080x1920, stride 4352, 32 bpp),
   the touch through evdev, the NFC through the BCM2079x.
2. **The vendor's own stack, on our kernel.**  The ROM's Android
   ramdisk (unpacked from a dump of the stock boot partition) was
   booted with our kernel.  That gives the vendor camera HAL, running
   against our driver, which is the only way to see what it *asks*
   for.
3. **The vendor's requests, captured.**  Printk in the kernel's ioctl
   handlers -- `msm_isp_request_axi_stream`, `msm_isp_cfg_axi_stream`,
   `msm_isp_cfg_input` -- recorded the vendor's stream request, its
   plane geometry and its CAMIF input.  The capture is in
   [archive/vfe-request-2026-11-11.txt](archive/vfe-request-2026-11-11.txt).
   The dump of the stock camera service is in
   [archive/media.camera-2026-10-28.txt](archive/media.camera-2026-10-28.txt).
4. **A daemon for the camera, a socket to the firmware.**  `camd` owns
   the V4L2/ISP state machine; the firmware's `sensor` module is only
   its client.  The two talk over a unix socket
   (`/data/krux/camd.sock`) with a small protocol: `F` for a frame,
   `A` for "I am reading", `E` for the exposure and gain, `C` for a
   crop, `D` for a dump.
5. **The measurements, then the arithmetic.**  Every claim about the
   hardware was measured, and the numbers were then used to *predict*
   the next number.  The band that haunted the preview for a week was
   identified by arithmetic, not by another capture: the picture was
   1,228,800 bytes in a buffer read at a 3,280-byte stride, so it had
   to appear in exactly `1280*960/3280 = 374` rows -- and it did.

## 2. The tools, and where they live

| what | where |
|---|---|
| the kernel (out-of-tree build, air-gap profile) | `tools/build-kernel.sh --tree linux --out <dir>` |
| the camera daemon | `scripts/cam/build-camd.sh` |
| the firmware (MicroPython, arm32, static) | `scripts/build/build-micropython.sh` |
| the boot image | `rootfs/build-stub-ramdisk.sh`, `rootfs/make-boot-image.sh` |
| the deployment (push the tree to the device) | `scripts/deploy-firmware.sh` |
| the firmware's tests | `scripts/misc/run-krux-tests.sh` |
| the camera's state | `docs/cam-state.md` |
| the vendor's CPP firmware | `cam/fw/cpp_firmware_v1_2_0.fw` (from `/vendor/firmware`) |
| the vendor's ISP configuration blobs | `cam/data/` (426 files, tracked) |

The one-off diagnostics of the bring-up are kept in
`artifacts/cam-2026-11-11/` (gitignored): the vendor captures, the
boot images that were used, the framebuffer dumps and the small tools
that were written on the day (the touch injector, the control-socket
clients).

## 3. What the hardware turned out to be

* **The sensor** delivers 3280x2464 (plus a border the CAMIF counts),
  in a binned 1640x1232 mode and a full 3280x2464 mode.
* **The CAMIF** window is 3280x2464, the input clock 260 MHz, and the
  CAMIF's own `epoch_line1` is 0 on the preview path.
* **The ISP** has two pixel sources: `CAMIF_RAW` (the Bayer mosaic)
  and `PIX_VIEWFINDER` (its own processed output, 1280x960 in the
  vendor's configuration).  The vendor's preview is
  **PIX_VIEWFINDER diverted into the CPP**: the ISP's buffer goes to
  the CCP, which writes the picture the panel shows.
* **The CPP** runs firmware from `/lib/firmware/cpp_firmware_v1_2_0.fw`
  and takes a per-frame message (the chromatrix) that carries the
  picture's geometry.  The vendor's preview message is 266 words and
  is written for 1280x960.
* **The panel** is a 1080x1920 framebuffer with a stride of 4352 and
  two buffers, panned by the display commit ioctl.  A framebuffer
  dump therefore shows *two* pictures, and the first one can be stale.
* **The NFC** is a BCM2079x over I2C, with its own transport layer
  (`nfc/`, `krux/src/nfc.py`).

## 4. The lessons, in the order they were learned

* **The buffer must be the CAMIF's, not the sensor's.**  The ISP-IF
  does not crop; a buffer for the sensor's active size is 91,648
  bytes too small and the frame comes back empty.
* **Copy the vendor's request exactly.**  NV21 and not NV12, the
  chroma plane's own format (CRCB, 3) and not CBCR, its scan lines
  (512 and not 480), `y_only=0`, `init_frame_drop=1`, `divert=1`,
  `epoch_line1=0`.  Every one of those was a difference between the
  request that worked and the one that did not.
* **The kernel's counters are the first signal.**  `image master 0 bus
  overflow` and `VFE0 pix0 overflow` in dmesg say that the geometry
  is wrong long before the picture looks wrong.
* **A stride is not a width.**  The band was a buffer read at the
  wrong stride.  When a picture comes out in bands, work out what
  stride would produce exactly that pattern before capturing anything
  else.
* **The denoise is not free.**  The ISP's processed output is
  smoother than the raw; that is what the vendor's preview is, and
  the entropy capture has to live with it (5.2 bits/px, deviation 38).
* **The autofocus needs a gate that the scene can pass.**  The sweep
  only ran when the frame's mean was at least 20; the processed path
  in a dim room sits at 17-19, so the lens never moved.  The gate is
  now 10, and the score is divided by the mean so a dim frame still
  compares fairly.
* **The framebuffer lies twice.**  It is double buffered (so the
  first picture can be stale) and it is not what the panel shows (the
  panel follows the pan offset).

## 5. On the next device

The same order, and these are the questions to answer first:

1. What does the vendor's HAL *ask* for?  (Printk in the ioctl
   handlers, vendor stack on our kernel.)
2. What is the buffer's real geometry, and what stride does the
   picture have inside it?
3. Which pixel source does the preview use, and does it go through
   the post-processor?
4. What does the panel's framebuffer look like -- one buffer or two,
   and is it panned?

And the two habits that paid off: measure before believing, and let
the numbers predict the next measurement.
