# Continuation prompt

Paste this into a fresh session to pick up where `STANDALONE-FINDINGS.md`
leaves off. Read that file and `airgap-standalone.log.md` first — this is a map,
not a substitute.

---

## Situation

We are building an **air-gapped signing device** from a Nexus 5 (`hammerhead`):
no modem, no Wi-Fi, no Bluetooth, running our own minimal userspace with
[Krux](https://github.com/selfcustody/krux) rather than Android.

**Phase complete.** A freestanding ARM `init` running as PID 1 — no TWRP, no
Android userspace, no console, a 4.8 KB initramdisk — has been shown to:

- drive the 1080×1920 panel (mmap + `MSMFB_DISPLAY_COMMIT`)
- read touch (`/dev/input/event1`)
- **talk to NFC**: `poll()` reports POLLIN, 6 bytes read, response begins
  `10 60` (MT header)
- **power the device off** via `reboot(POWER_OFF)`

Verified on two independent kernels.

**Our own kernel now builds** (2026-10-19): CAF 3.4
`lineageos_hammerhead_defconfig` + the air-gap profile, gcc 4.9, out-of-tree.
`zImage` 5.9 MB, 1101 options, zero errors, `CONFIG_USB_FUNCTIONFS=y`, no
radio. **Not yet flashed** — the four capabilities have not been re-verified
on it, and `adb devices` has not been shown to reach `device` rather than
`offline`.

**A new requirement is open: the camera** (§12, §14). Krux needs it for QR
reading (BBQr, UR, animated UR) and for entropy from images. Everything
needed to plan it is now established; see the two sections linked above.

Everything we own is built and read-only from the device's point of view, so
nothing here touches the air-gap or signature material.

---

## Next steps

### (a) Flash our kernel and re-validate the four capabilities

The kernel is built. Next:

```sh
./build/build-kernel.sh --tree "$PWD/standalone/vendor/android_kernel_lge_hammerhead" \
                         --out /tmp/opencode/ko
# -> /tmp/opencode/ko/arch/arm/boot/zImage
```

Then `fastboot boot` it with `standalone/rootfs/standalone/` unchanged, and
confirm all four capabilities reproduce. Recovery if it hangs: Vol+ + Power
into fastboot.

Two kernel changes to make **first**, both small:

1. **`CONFIG_DEVTMPFS=y` + `CONFIG_DEVTMPFS_MOUNT=y`.** Off in every existing
   kernel, which is why "no devtmpfs" was recorded as a fact. It is available
   in our tree (`drivers/base/Kconfig:24`, needs `HOTPLUG`, which is `y`).
   Turning it on makes the camera's **dynamically allocated** major work
   (`alloc_chrdev_region` in `msm_camera.c:4007`) — nodes cannot be baked into
   the cpio — and gives us `/dev/ffs.ep0` for adb for the same reason. It
   also makes the baked fb0/event1/bcm2079x nodes redundant.
2. **The camera options** (§14.5 Phase A): `CONFIG_MSM_CAMERA=y`,
   `CONFIG_OV5640=y`, `CONFIG_MSM_CAMERA_SENSOR=y`, `CONFIG_MSM_ACTUATOR=y`,
   `CONFIG_MSM_CAMERA_FLASH=y` — and explicitly **off** `MT9D112`, `MT9P012`,
   `MT9P012_KM`, `MT9T013`, `S5K3E2FX`, which are `default y` in
   camera_v1/Kconfig and would otherwise be probed on the camera's I2C bus.

The air-gap profile's own verification step re-audits the result
automatically; it is not enough to edit the defconfig by hand.

### (b) Validate adb

`CONFIG_USB_FUNCTIONFS` is off in **all three** existing kernels. Measured
consequence: adbd starts, finds no `/dev/ffs.ep0`, and sits at `offline`
forever. `/dev/usb-ffs/adb` exists but adbd creates that itself — it is not
evidence of anything.

Our built kernel has `CONFIG_USB_FUNCTIONFS=y`. It is a **choice value**
(§9a), so winning the choice meant clearing 29 siblings, including
`USB_G_ANDROID`, `USB_ETH`/`RNDIS` and `USB_MASS_STORAGE`. The USB
*controller* (`USB_GADGET`) is untouched.

Verify `adb devices` reaches `device`, not `offline`.

### (c) The camera bring-up — before any MicroPython

Krux needs the camera in the **first milestone**, so this comes before (d).
The order below is a dependency, not a preference.

1. **Write a small static C client** for the native camera API
   (`/dev/config0`, `/dev/control0`, `/dev/frame0`): `GET_CAMERA_INFO` →
   `SENSOR_IO_CFG` (this is where hammerhead's sensor GPIOs/regulators/clocks
   go — they are **not** in the tree; there is no `board-8974-camera.c`) →
   `CONFIG_VFE` → `ENABLE_VFE` → `GETFRAME` → mmap → copy →
   `RELEASE_FRAME_BUFFER`.
2. **Blit the captured frame to the framebuffer.** The panel is a perfect
   observable: if the camera works we see the room on the Nexus 5's screen,
   with no adb and no serial console in the loop. It also settles whether
   hammerhead's camera config is right.
3. **Time the QR decode** on the device. `quirc` (BSD, self-contained C, what
   OpenMV uses) is the decoder; it is not fast, and the *decode* rate — not
   the capture rate — is the unknown. This measurement is only possible once
   step 2 works.

No vendor blob is needed: `include/media/msm_camera.h` in our tree is a
userspace-includable copy of the native ioctl ABI, and there is no
`request_firmware` anywhere in `camera_v1`.

### (c2) The camera: what is now known (read STANDALONE-FINDINGS.md §15, §16)

The camera is a **replacement for the CAF "imaging server"**, not a V4L2
capture program. `camera_v2` forwards every V4L2 request to userspace and
blocks until it is acknowledged.

**Already proven on the live device** (`standalone/cam/imaging4.c`): we
subscribed to the private event type, received the kernel's NEW_SESSION
event, ACKed it, and the blocked `open()` of the rear sensor node went from
`ETIMEDOUT` to `EFAULT` — i.e. the handshake completed and the kernel
resumed into the part the real daemon does.

Two traps, both found by experiment:

- `v4l2_event_subscribed()` in this 3.4 tree matches type **and id exactly**.
  There is no `id == 0` wildcard. **Subscribe to ids 1..7 separately** or the
  kernel waits forever.
- The ioctl number encodes `sizeof(struct v4l2_event)`. The *device's* kernel
  wants 128 bytes; **our tree wants 136**. Use the tree's header for our own
  kernel. Do not "fix" a mismatch by changing sizes — find out which header
  the kernel under test was built with.

Remaining work, in order: enable the camera options in our kernel and build
(§14.5 Phase A) → port `imaging4.c` to the tree's ABI → work the command
list (`QUERY_CAP`, `S_FMT`, `STREAM_ON`), using the framebuffer as the
observable → the ISP tuning data, which on Android comes from
`/vendor/etc/camera/` in the ROM.

**Do not** try to reuse the device's struct layouts: its
`struct msm_v4l2_event_data` has one word more than ours before
`arg_value`, and its `session_id` is a pointer.

### (d) MicroPython + Krux

`standalone/vendor/micropython` is at `9a3a0c0`. The plan is a freestanding
MicroPython `minimal` port so the whole payload stays libc-free, matching what
already works.

Constraint that matters: **our `init` must remain a single static binary with no
dynamic linking.** `prebuilts/gcc-androideabi-4.9` is the AOSP prebuilt, but
note its Python wrappers have a `#!/usr/bin/python` shebang that was rewritten
to `python3` — and sed-ing through symlinks turns them into regular files and
breaks them.

Krux is vendored at `standalone/vendor/krux` (commit `9b3a431`). It needs five
native modules from the MicroPython port: `sensor`, `image` (with
`find_qrcodes`), `shannon`, `qrcode`, and a board-specific `lcd`. See §14.

**The QR decoder is settled**: `selfcustody/k_quirc` is Krux's own fork of
OpenMV's quirc, a standalone C library with a one-shot
`k_quirc_decode_grayscale(gray, w, h, ...)` API and inverted-QR support
(needed for `find_qrcodes(find_inverted=...)`). Port that; do not write a
decoder.

The firmware's `sensor` shim only needs: `reset(freq=)`, `get_id()`,
`set_pixformat()`, `set_framesize()`, `set_windowing()`, `set_hmirror()`,
`set_vflip()`, `run(0/1)`, `snapshot()`, `skip_frames()`, `__write_reg()`.
`snapshot()` must return an object with `find_qrcodes()`,
`get_statistics()` (`.l_stdev() .a_stdev() .b_stdev()`), `to_bytes()`,
`width()`, `height()`. The Krux **simulator** (`simulator/kruxsim/mocks/`)
is the best spec of these semantics — it uses true OpenMV LAB statistics,
where the mobile app approximates with RGB.

Note `payload()` returns `str` in both reference mocks but `bytes` on real
hardware; pick one and make `src/krux/qr.py` accept it.

Before starting: **re-check `AT_HWCAP`.** The standing soft-float mandate rests
on `AT_HWCAP_FP` being clear, but `/proc/cpuinfo` reports `vfp neon vfp3 vfp4`
and both kernels build with `CONFIG_KERNEL_MODE_NEON=y`. If the FPU is
genuinely present the mandate is belt-and-braces, and that frees us to build a
*hosted* MicroPython rather than only the freestanding `minimal` port.

---

## Working practices that earned their place

**Read the driver, not the HAL.** Both NFC fixes — the 100 ms power settle and
the `poll()` before `read()` — came from reading
`drivers/nfc/bcm2079x.c`. `read()` is a single-shot I²C receive that does not
block; we had been asking for a reply that did not exist yet.

**Never derive device behaviour from a source tree we know is different.** Our
kernel tree has no KernelSU anywhere; the ROM was built from a patched tree we
do not have. Assuming otherwise produced two wrong conclusions in one session.

**Generate, never transcribe.** `LINUX_REBOOT_CMD_POWER_OFF` was `0x4321`
instead of `0x4321FEDC` for several runs because it was recalled rather than
read; the header is now vendored. The screen font is rasterised from a TrueType
face by `gen-font.py`, not written out by hand.

**Tripwires beat comments.** `main.c` carries compile-time assertions on
`sizeof(struct fb_var_screeninfo) == 160` and
`sizeof(struct mdp_display_commit) == 168`. Those fired on the first build and
caught a 112-byte struct being handed to a 168-byte ioctl. `verify-constants.py`
runs 24 checks against the kernel tree and should be run before every build.

**Distrust "it did not crash".** A void-returning probe cannot distinguish
success from a failed `open()`. Every result is now drawn on screen as text.

**When a report does not match the design, fix the design.** Six runs of
bar-encoded results produced six different misreadings. The encoding was the
defect. 320 bytes of bitmap font ended it.

---

## Traps

- `FB_STRIDE` (4352) is **bytes**. A row is 1088 `u32` = 1088 pixels, 1080
  visible.
- The panel is **command-mode DSI**: nothing scans out, a frame must be
  committed.
- No devtmpfs **in the existing kernels**. Device nodes must be baked into the
  cpio as newc device entries; `mknod(2)` returns `EINVAL`. This is fixable
  *in our own kernel* — `CONFIG_DEVTMPFS` is available and should be turned
  on, because the camera's major is dynamic and cannot be baked.
- The camera's char-device major is **dynamic**
  (`alloc_chrdev_region`, `msm_camera.c:4007`). Node *names* are fixed
  (`/dev/control0`, `/dev/config0`, `/dev/frame0`, `/dev/pic0`); the major is
  not knowable at build time.
- Fill **both** fb planes before every commit, or the panel shows garbage.
- `bootimg.py` header addresses are not ours to choose. The bootloader honours
  them literally; wrong values boot to nothing with no error.
- `boot-kernelsu-20260926.img` contains **no KernelSU** despite its name — it is
  TWRP's kernel with the ROM's ramdisk.
- The device's *installed* kernel (`e4e483b4ef6`) **has** KernelSU. Do not
  confuse it with the ROM zip's kernel (`g11370a8eb99`), which does not.
- `/proc/config.gz` is trustworthy; the config embedded in the image is not
  extractable from these CAF payloads.