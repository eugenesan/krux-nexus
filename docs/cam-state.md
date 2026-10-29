# STATE — the camera, in one page

**The camera has never worked under our kernel.**  The vendor's own
camera app fails the same way ours does, so this is not our
imaging server's fault; it is our kernel.  Everything below is
measured, and everything below was measured *under our kernel*.

## Where it stops

| stage | state |
|---|---|
| the sensor streams | **OK** -- 11,808 packets, no ECC, no CRC errors |
| the PHY delivers them | **OK** -- the power configuration reads 0x3f |
| the CSID receives them | **OK** -- but **none of them is mapped** |
| the ISP-IF routes VC 0 to PIX0 | **OK** -- the vendor's own values |
| the CAMIF is enabled, with the vendor's geometry | **OK** -- 3280x2464 |
| the ISP's source state and configuration | **OK** -- the vendor's values |
| the CPP takes the chromatrix | **OK** -- 266 words, **accepted: status 0** |
| the CPP's buffer queue | **OK** -- four buffers, the right identity |
| a frame arrives | **YES** -- "GOT A FRAME: index=2" |
| **the frame has pixels** | **NO** -- `bytesused=0`, all four buffers are zero |

## The three things that were broken, and are fixed

1. **The ION heap masks** (`cam/ion.h`).  `heap_id_mask` is
   a mask of heap **ids**, not of heap types; `0x00010000` is
   `ION_CP_WB_HEAP_ID`, a **8660-only** heap that this 8974 does not
   have, so every buffer allocation failed with `ENOENT`.  The
   system heap is id 30 (`1 << 30`).
2. **The kernel oopsed** when the CPP firmware could not be loaded
   (`cpp_load_fw()` dereferenced a NULL firmware).  Fixed in our
   tree.
3. **The CPP firmware was never loaded**, so the chromatrix was
   rejected ("invalid fw version 00000000") and the kernel oopsed.
   It has to be **version 1.2.0** (`130 + 27*5 + 1 = 266` = the
   chromatrix's word count) and it has to be in `/lib/firmware` under
   its bare name.

With those three fixed, the chromatrix **is** accepted and a frame
**does** arrive.  It is empty.

## The finding that reframes the work

The ROM's **own** camera app, running under **our** kernel with the
**vendor's** HAL:

```
CSID0 LUT: vc0 0x0000122b vc1 0x00000000, cfg0 0x00000023 cfg1 0x00000013
CSID0 irq status 0x000002dd, packets 2363548, unmapped long 0x00000000,
                mapped long 0x00000000, short 0x0000011a, ecc 0, crc 0
```

**2,363,548 packets, none of them mapped, and exactly the same 282
"short" packets as our imaging server sees.**  So the fault is in
**our kernel**, and the imaging server is as good as the vendor's
HAL.

**And the packets are on the wrong virtual channel.**  The captured
long packet headers say so:

```
unmapped long 0x2b010405   data type 0x2b, VC 1, 1029 words
mapped   long 0x1201040a   data type 0x12, VC 1, 1034 words
```

Both data types arrive on **VC 1**, while the CSID's LUT -- as the
vendor's HAL programs it -- maps **VC 0**.  So the packets are not
mapped at all, and the CAMIF has nothing to work with.

(The device tree is not the reason: the CSID node in the ROM's DTB
carries no virtual channel or data type -- those come from the
HAL's ioctl -- and the sensor's register write-back after our
programming is identical to the vendor's.)

## The next measurement

The same numbers, **in the ROM's kernel**, where the camera works:
the CSID's LUT and counters, and the VFE's registers.  The
difference between the two is the bug.  The ROM's kernel is
`ge4e483b4ef6`, which is not in our tree, so the comparison has to
be made on the device:

```
adb reboot bootloader
fastboot boot <the ROM's boot.img>          # the ROM's kernel + Android
adb shell su -c 'dd if=/dev/mem bs=4 skip=... '   # the CSID's registers
```

The CSID's base is `0xfda08000` (from the device tree) and its
counters are at the offsets the driver prints.

Until that comparison is made, the camera stays where it is: the
whole chain is configured correctly and the pixels do not arrive.

---

# STATE — 2026-10-21 (the touch/keys session)


## Camera (the main thread)

Everything below is **measured**, not assumed:

```
sensor streams              OK   11,808 packets, 0 ECC, 0 CRC errors
PHY delivers them           OK   power cfg 0x3f
CSID accepts and maps them  OK   LUT vc0 0x122b, cfg0 0x23, cfg1 0x13
                                  (now byte-for-byte the vendor's own)
ISP-IF routes VC 0 -> PIX0  OK   CTRL_0 0xc0, CID_MASK0 0x02, INTF_CMD 0xfffffffd
CAMIF enable                OK   camif_update 1 (ENABLE_CAMIF)
CAMIF geometry              OK   0x300 = 2464x3280, 0x304 = 0..3279, 0x308 = 0..2463
                                  (exactly the vendor's values, captured)
ISP source state / config done  OK   SET_SRC_STATE ok, CONFIG_DONE ok
CPP takes the chromatrix    OK   len 140 == expected 140, 266 words sent,
                                  **accepted: status 0**, queued to the hardware
CPP buffer queue            OK   ENQUEUE_STREAM_BUFF_INFO, 4 buffers, identity
                                  0x10000 (session 1, stream 0)
CAMIF rejects the frame     OK   no violation any more (the cid was the cause)
a frame arrives             YES  "GOT A FRAME: index=2"  <- 2026-10-20
the frame has pixels        NO   bytesused 0, all four buffers all zero
```

### What made the frame arrive (2026-10-20)

Two things, both found by *running the vendor's camera under our
kernel* and letting our kernel log what it does:

1. **The chromatrix has to be sent once per frame.**  The kernel logs
   the vendor HAL's ioctls; the HAL sends `VIDIOC_MSM_CPP_CFG` (the
   chromatrix) **once per frame** (256 times for 256 frames), not once
   at configuration time -- the kernel patches the frame's current
   buffer addresses into the message when it accepts it, and the
   buffers rotate.  Our imaging server now sends it per frame
   (`cpp_send_chromatrix()` in `imaging8.c`).

2. **The CSID's data type was wrong.**  The vendor's own CSID
   configuration, read out of the chip's registers while the ROM's
   camera ran:

   ```
   CSID0 LUT: vc0 0x0000122b vc1 0x00000000 vc2 0x00000000 vc3 0x00000000,
              cfg0 0x00000023 cfg1 0x00000013
   ```

   The CSID's LUT holds one byte per channel: the routed channel 0
   carries data type **0x2b** with decode format **2**, and channel 1
   carries 0x12 with decode format 1 (the CFG register is
   `(decode_format << 4) | 3`, which is why cfg0 is 0x23).  We had
   0x12 on channel 0: the packets then stayed unmapped (the CSID's
   "captured unmapped long packet header" read 0x2b010405 -- the data
   type the sensor really sends) and no pixel ever reached the ISP.
   Our CSID configuration is now byte-for-byte the vendor's.

### The last gap: the frame is empty

The ISP delivers a buffer (`GOT A FRAME: index=2`) but it holds no
pixels: `bytesused=0`, and all four of our buffers are all zero.
So the frame-done interrupt fires while the data never lands in the
buffer.  What has been ruled out:

* the buffers map fine (no `buf has null/error ION handle`, no
  `cannot map address` in the log) -- the ISP's buffer manager
  imports our ION fds with `ion_import_dma_buf`, which is right;
* the per-frame register replay is not the cause (the frame is still
  empty with `nocycle`);
* the CPP takes the chromatrix and the chromatrix is accepted;
* the CSID's configuration is now byte-for-byte the vendor's, so the
  packets are mapped and reach the CAMIF;
* the CAMIF's geometry and its CAMIF_INPUT are the vendor's
  (0x2e8 = 3, 0x300 = 2464x3280, 0x304 = 0..3279, 0x308 = 0..2463).

What has *not* been compared yet: the vendor's **ISP register
configuration** against ours, register by register.  The kernel now
dumps the VFE's registers on every interrupt (the first three and
then every hundredth), so the two can be compared.  The first
comparison is not conclusive because the two dumps are of *different
states*:

* ours is the raw path (CAMIF_RAW): the CAMIF block (0x2c4-0x320) is
  live, and the CAMIF's own registers read 0x2e8 = 3, 0x300 =
  0x09a00cd0, 0x304 = 0x00000ccf, 0x308 = 0x0000099f, 0x314 =
  0xffffffff -- all the vendor's values;
* the vendor's dump was the *preview* (PIX_VIEWFINDER) state: its
  CAMIF block reads all zero, and it has a set of registers
  (0x174-0x1fc) that ours does not.

So the next comparison needs the vendor's *raw* state: the ROM's
camera app has to take a picture, not just preview.  (The screen has
to be on for the app to preview at all -- `input keyevent 224` wakes
it; that is the same reason the NFC framework is quiet with the
screen off.)

The registers that differ in the common part are the buffer address
registers (0x070, 0x074, 0x080, 0x084, 0x094, 0x098, 0x0a4, 0x0a8 --
different buffers, expected), the interrupt mask (0x030: 0x04020c00
against our 0x00000002) and 0x040/0x04c (0x00000c03/0x0000000c against
our 0x00000003/0x00000003) -- those two are the interesting ones.

### The vendor's camera, captured

The kernel logs every ISP and CPP ioctl, so running the ROM's camera
app under our kernel gives the vendor's exact sequence:

```
per frame:  ISP UPDATE_STREAM (0xc01056cd)
            ISP ENQUEUE_BUF   (0xc05056c2) x5
            ISP VFE_REG_CFG   (0xc00c56c0) x7..8   <- the register cycle
            CPP CFG           (0xc01056c0)          <- the chromatrix
            CPP DQEVENT       (0x80805659 = VIDIOC_DQEVENT)
            CPP GET_EVENTPAYLOAD (0xc01056c1)
            ISP ENQUEUE_BUF   (0xc05056c2)
            ISP CONFIG_DONE   (0xc00456ce)
            ISP UPDATE_STREAM (0xc01056cd)
```

`0x80805659` is `VIDIOC_DQEVENT` (`_IOR('V', 89, struct v4l2_event)`),
not a private ioctl.

### The CPP path (solved 2026-10-20)

The chromatrix ioctl is `VIDIOC_MSM_CPP_CFG` (0xc01056c0) and it now
**works**.  Four things had to be right, and each of them is a trap:

1. **The ioctl numbers are the camera_v2 ones, not the camera_v1 ones.**
   The camera_v2 driver includes `<media/msmb_pproc.h>`, where the CPP's
   ioctls are numbered from `BASE_VIDIOC_PRIVATE`:

   | ioctl | nr |
   |---|---|
   | `VIDIOC_MSM_CPP_CFG` | +0 |
   | `VIDIOC_MSM_CPP_GET_EVENTPAYLOAD` | +1 |
   | `VIDIOC_MSM_CPP_LOAD_FIRMWARE` | +3 |
   | `VIDIOC_MSM_CPP_ENQUEUE_STREAM_BUFF_INFO` | +6 |
   | `VIDIOC_MSM_CPP_DEQUEUE_STREAM_BUFF_INFO` | +7 |
   | `VIDIOC_MSM_CPP_APPEND_STREAM_BUFF_INFO` | **+17** |

   `include/media/msm_camera.h` (the camera_v1 header) has a *different*
   numbering: its `+1` is GET_EVENTPAYLOAD and it has no APPEND at all.
   Using the camera_v1 numbers sends the "append" to GET_EVENTPAYLOAD,
   which dereferences a NULL and **panics the kernel**
   (`msm_cpp_subdev_ioctl+0xe30`, NULL at 0x34).  The ispif.h in
   `cam/` now carries the camera_v2 numbers.

2. **The buffer queue is created by ENQUEUE, not APPEND.**  APPEND only
   adds buffers to a queue entry that already exists; ENQUEUE creates
   the entry (and then adds the buffers).  With APPEND the CPP says
   `error buffer queue entry for sess:0 strm:0 not found`.

3. **The identity carries the session and the stream**: the CPP looks
   the queue up by `(identity >> 16) & 0xffff` (session) and
   `identity & 0xffff` (stream).  The identity is in *our* frame struct
   (it is copied from user space), so it has to be set -- the camera2
   HAL uses `(session_id << 16) | stream_id`.  Left zero, the CPP looks
   for session 0 and finds nothing.

4. **`frame.status` must point at an int32.**  The CPP writes the result
   of the frame through it; left NULL the copy back fails and the whole
   ioctl is reported as an error *even though the chromatrix was
   accepted*.

Also: the kernel copies `num_buffs` **buffer_info** structures, so the
array has to be that long.  With one entry and `num_buffs = 4` the
kernel reads three structures of stack garbage, which shows up as
`error buffer index already queued` and `ION import failed`.

The frame struct (`msm_cpp_frame_info_t`) is byte-exact against
`include/media/msmb_pproc.h` -- `struct timeval` is two longs on
32-bit ARM, and `struct ion_handle *` is a pointer.

### The remaining gap

The chromatrix is accepted and queued to the CPP's hardware, but the
frame never completes: the CPP's timer expires (`fatal: cpp_timer
expired for identity=0x10000`) and the CAMIF reports a violation.

Two kernel bugs are visible in that path, both worth fixing in *our*
kernel:

* `cpp_load_fw` (the firmware reload after the timer expiry) calls a
  NULL `io_w` (`msm_camera_io_w+0x0`) and oopses.  The reload path
  needs a NULL check.
* the CAMIF's violation (status 0x1) is back, now that the source
  state is live.

The camera2 HAL's order is worth re-checking: it may send
SET_SRC_STATE/CONFIG_DONE *before* the CPP's chromatrix, and it may
also set the CPP's clock (`VIDIOC_MSM_CPP_SET_CLOCK`) and the strip
count, so that the frame's timing matches the sensor's real pixel rate
(266.67 MHz against 3280x2464 -- see the note below).


Measured constants (do not re-derive):
- MIPI data type **0x12** (the header's type byte) — swept over 0x12..0x37;
  only 0x12 maps the packets.
- virtual channel **0** (the vendor's ISP-IF routing says `cid0 0`).
- the vendor's ISP-IF routing: vfe_intf 0, intftype 0 (PIX0), 1 cid, cid 0,
  csid 0, **crop disabled** (0..0).
- the vendor's CAMIF config: pix_clk **260 MHz**, pixels_per_line 3280,
  lines_per_frame 2464, first_px 0, **last_px 3279**, first_line 0,
  **last_line 2463**, camif_input **3**.
- the ISP is the **vfe40** (/dev/v4l-subdev13); subdev14 is the 8960's
  vfe32 and driving it takes the device down.

The two ioctls the camera2 HAL sends and we did not: SET_SRC_STATE (the
pixel source is live) and CONFIG_DONE (the configuration is complete).
Both are now sent and accepted.

The CAMIF's geometry is now the vendor's exactly and the violation is
gone for the clock values tried (the divider only produces 266.67 MHz).

The **CPP** used to be the last piece; it now takes the chromatrix (see
above).  What is left is the frame itself:

The CAMIF still rejects the frame.  Its geometry is now exactly the
vendor's (0x300 = 2464x3280, 0x304 = 0..3279, 0x308 = 0..2463) and the
vendor asks for a 260 MHz pixel clock -- but the divider gives 266.67 MHz
(the vendor would get the same).  The CAMIF checks the frame timing
against that clock, so the **sensor's real pixel rate has to be
266.67 MHz**, which does not match 3280x2464 at any sane frame rate.
Next: find the frame rate the vendor's sensor mode actually runs at
(the 92-entry page sequence sets the sensor's PLL) and match the CAMIF's
clock to it; then the ISP's frame delivery (the VB2/SFE path) is the
last piece.

## Device access — one device, two threads

The camera thread and the MicroPython/Krux thread both need the single
Nexus 5, and they need it in *different* states: the camera thread needs
it in fastboot and then in TWRP with **our** kernel, the MicroPython
thread needs it in the ROM's Android with root.  A `fastboot boot` kills
the adb connection the MicroPython work depends on, and the camera
run's reboot (the failure path) kills an in-flight MicroPython run.

**So: one thread owns the device at a time, the other waits.**  The
camera's boot+run cycle is atomic (about 3-5 minutes): `adb reboot
bootloader` -> `fastboot boot` -> wait for adb -> run `imaging9` ->
collect the output and the kernel log.  The MicroPython work happens
between camera cycles, on the ROM's Android.

The kernel log of a crashed camera run is read from the ROM's Android
afterwards: `su -c "cat /proc/last_kmsg"` holds the previous boot's
log, which is where the oopses above came from.


## MicroPython / Krux (background)

**MicroPython 1.28.0, static arm32, running inside TWRP** ✓
(`kruxport/micropython-arm32` ✓, built by
`scripts/build/build-micropython.sh` ✓):
```
$ adb shell /data/local/tmp/micropython -c "import sys; print(sys.implementation)"
MicroPython (name='micropython', version=(1, 28, 0, ''), ...)
```
Buildroot cannot do this: `BR2_PACKAGE_MICROPYTHON` depends on
`!BR2_STATIC_LIBS` ✓, and TWRP's libc is bionic ✓ so a dynamic binary
would not run there ✓.  The hand build needs the host tools (mpy-cross)
built with the *host* compiler ✓ (otherwise the port tries to run an
arm32 mpy-cross ✓) and the armel sysroot ✓.

The port's scope and the Linux replacements are in
`kruxport/README.md` ✓.

### `machine.I2C` — done and verified on the hardware (2026-10-20)

Krux's firmware talks to its peripherals over MaixPy's `machine.I2C`
(`krux/src/krux/i2c.py`: `from machine import I2C`, then
`I2C(I2C.I2C0, freq=400000, scl=..., sda=...)`).  MicroPython's unix
port has no I2C at all, and the generic class in
`extmod/machine_i2c.c` is not usable here: it wants a port-side HAL
(`mp_machine_i2c_p_t`) and MaixPy's API differs anyway (`mem_size`
rather than `addrsize`, the bus chosen by the `I2C.I2C0` class
constant, `writeto` taking the bytes rather than a list).

So the port has its own class:
`kruxport/machine_i2c_linux.c` ✓, applied to the tree by
`kruxport/apply-port.sh` ✓ (called from
`build-micropython.sh` ✓) as `ports/unix/machine_i2c_linux.c` ✓, and
added to the `machine` module through the port's
`MICROPY_PY_MACHINE_EXTRA_GLOBALS` hook ✓ (in `ports/unix/modmachine.c`
✓, which `extmod/modmachine.c` includes).

It talks to `/dev/i2c-N` with the i2c-dev ioctls.  Two things about
that are worth knowing:

* **the i2c-dev ABI has to be spelled out**: the cross build's sysroot
  has no kernel headers, so `struct i2c_msg`, `struct
  i2c_rdwr_ioctl_data`, `I2C_SLAVE_FORCE` (0x0706), `I2C_RDWR` (0x0707)
  and `I2C_M_RD` (0x0001) are in the file.  On 32-bit ARM `struct
  i2c_msg` is 12 bytes (the pointer needs 4-byte alignment).
* **I2C_SLAVE_FORCE, not I2C_SLAVE**: i2c-dev refuses `I2C_SLAVE` for
  an address a kernel driver has claimed (-EBUSY), and on the
  hammerhead every peripheral of interest is claimed.

Verified on the device (the ROM's Android, as root):

```
$ adb shell su -c "/data/local/tmp/micropython -c \"
from machine import I2C
b = I2C(I2C.I2C2)            # bus 84
print(b.readfrom(0x36, 1).hex())\""
d7
```

which matches a C probe of the same bus and address (the max17048 fuel
gauge).  The class constants are the hammerhead's three buses: I2C0 =
0 (the NFC's bus), I2C1 = 2, I2C2 = 84.

Three API traps in MicroPython 1.28, all hit and fixed:

* `STATIC` is **gone** (it was removed after 1.19); plain `static`.
* `mp_obj_malloc` takes **two** arguments now:
  `mp_obj_malloc(machine_i2c_obj_t, type)`.
* the frozen `asyncio` module already owns the qstrs **`n`** and
  **`len`** (they are its local variables), so the port's arguments
  must not use them -- `MP_QSTR_n` in the port's source makes the
  generated qstr enum have a duplicate enumerator and the *frozen
  content* fails to compile.  The port uses `nbytes`.

The NFC chip (bcm2079x) is **not** reachable over I2C from userspace
while the vendor driver holds it: the chip NACKs
(`ENOTCONN` from the QUP driver).  The vendor's own path is the
`/dev/bcm2079x` misc device with its six ioctls -- see
`nfc/TRANSPORT.md`, which has the full contract (framing
measured on the hardware, the power/wake ioctls, and the AOSP HAL
source to port).  That is the way to the NFC, not raw I2C.

- `rootfs/buildroot.fragment`: MicroPython 1.28.0 + micropython-lib
  added; unix-ffi off (it needs libffi, whose autoreconf fails here);
  `BR2_RELRO_NONE=y` (BR2_RELRO_FULL selects BR2_PIC_PIE, and a PIE /init
  hangs the bootloader on this kernel); `BR2_STATIC_LIBS=y` (TWRP is bionic,
  so the runtime must be static).
- `rootfs/build.sh`: the buildroot path was wrong
  (`rootfs/../../rootfs/buildroot` -> `buildroot`).
- the build extracts into TMPDIR -> set to `rootfs/build/tmp`
  (on the disk, not the tmpfs).
- the host gcc's "fixinc" step (host-gcc-final) fails intermittently;
  delete build/build/host-gcc-final-* and build/build/gcc-* and retry.
- the stale `build/host` tools (configured for the old `b/rootfs` path) had
  to be deleted together with the `build/build/host-*` stamps.
- **Runtime model**: we cannot replace the boot init ramdisk, so MicroPython
  runs *inside TWRP* (piggyback on TWRP's init). A static arm32 binary
  pushed to /data and run there.

The port's scope (from the Krux sources): the firmware is **MaixPy**
(a MicroPython fork) and the Python side is `src/krux/*.py`; the MaixPy
modules to replace with Linux ones are `board`, `machine` (I2C, WDT), `lcd`
(/dev/graphics/fb0), `Maix`.GPIO + `fpioa_manager` (the buttons and the
LED), `sensor` (V4L2) and `flash`. The NFC protocol is in Python
(`seedkeeper.py` does `import nfc`; the phone side is an ISO-DEP reader
over the BCM20793 on I2C bus 0 at 0x77).

### The NFC transport — the chip answers, standalone (2026-10-20)

The port's own C module `_nfc` (`kruxport/nfc_bcm2079x.c`,
compiled into the unix port by `apply-port.sh`) opens
`/dev/bcm2079x`, drives the power and wake lines, and reads and writes
whole packets.  **Verified on the device, in TWRP, with no vendor HAL
at all**:

```
$ adb shell 'cd /data/local/tmp && ./micropython nfc_test.py'
opened /dev/bcm2079x -> fd 3
power cycled
packet after power on: 10 60 00 02 00 01
wrote 4 bytes (GET_BUILD_INFO)
reply: 10 4f 04 24 00 44 65 63 20 32 32 20 32 30 31 32 31 34 3a 35 33 3a 35 30
        01 05 0d 46 05 1b 79 20 07 32 30 37 39 31 42 35
```

So **the chip's firmware is in its NVM**: it answers GET_BUILD_INFO
right after a power cycle, with no `.ncd` download.  The port therefore
does **not** need the DM/PRM patch-download state machine -- that is
the biggest piece of the NFC port, and it is not needed.  (The blobs
are kept in `nfc/blobs/` as a fallback.)

### The vendor's bring-up, captured byte for byte

Our kernel's `drivers/nfc/bcm2079x.c` now logs every packet that goes
to and from the chip (one line per packet).  Running the ROM's NFC
framework under our kernel gives the vendor's whole conversation, and
the port can replay it:

```
chip -> 10 60 00 02 00 01          Core Reset notification
->    10 2f 04 00                  GET_BUILD_INFO   -> "Dec 22 2012 14:53:50" "20791B5"
->    10 2f 2d 00                  GET_PATCH_VERSION
->    10 20 00 01 01               CORE_RESET (reason 1) -> 10 40 00 03 00 10 01
->    10 20 01 00                  CORE_INIT -> 10 40 01 18 00 03 0e 03 01 07 00 01 02
                                       03 81 84 83 0a 00 01 ff 3d 00 2e 00 15 0d 46
->    10 20 02 39 01 b9 36 ...     SET_CONFIG: the LPTD config (the .conf's LPTD_CFG)
->    10 20 02 66 0c cb 01 01 a5 . SET_CONFIG: NFA_DM_START_UP_CFG + PREINIT_DSP_CFG
->    10 20 02 06 01 b7 03 02 00 01   SET_CONFIG: the poll configuration
->    10 21 00 07 02 04 03 02 05 03 03   DISCOVER (GID_RF, OID 0) -> 10 41 00 01 00
->    10 20 02 0e 02 51 08 01 fe ff ff ff ff ff ff 58 01 70   SET_CONFIG (0x51)
->    10 20 03 02 01 52            CORE_SET_CLK  -> 10 40 03 05 00 01 52 01 01
->    10 2f 0c 01 00                PROP 0x0c
->    10 21 01 16 00 04 01 03 ...   RF 0x01 (the routing)
->    10 20 02 04 01 19 01 01      SET_CONFIG (0x19)
->    10 20 02 04 01 80 01 01      SET_CONFIG (0x80)
->    10 20 02 10 05 30 01 04 31 01 00 32 01 20 38 01 00 50 01 00  (5 poll params)
->    10 20 02 05 01 00 02 f4 01   SET_CONFIG (param 0x00)
->    10 21 03 0f 07 00 01 01 01 02 01 80 01 06 01 74 01 77 01   RF 0x03
->    10 21 06 01 00                RF 0x06
```

The module that replays it is `kruxport/kruxport/nfc.py`
(the `nfc` module Krux imports); it needs to be installed as `nfc.py`
on the device (or frozen into the firmware).

**Still missing**: the *discovery notification* and the *ISO-DEP
activation* -- they need a card in the field.  The phone has to be
held with a tag on its back while the ROM's NFC framework runs under
the logging kernel, so that the bytes of the tag notification and the
activation can be read off the log.

#### The ISO-DEP exchange, captured (2026-10-20)

With a card on the back of the phone, the ROM's NFC stack (the Krux
mobile app) did a complete transaction, and the kernel logged every
byte of it:

```
RX 10 61 06 02 01 00                  a tag was discovered (ISO-DEP)
TX 10 21 04 03 01 04 02               ACTIVATE (GID_RF, OID 4)
RX 10 41 04 01 00                     the activation: status 0
RX 10 61 05 21 01 02 04 00 ff 01 ...   the activation notification (OID 5)
TX 10 00 00 0d 00 a4 04 00 07 d2 76 00 00 85 01 01 00   <- the SeedKeeper SELECT
RX 10 60 06 03 01 00 01
RX 10 00 00 02 6a 82                  <- the card's answer: SW 6A82
TX 10 21 03 03 01 80 01               DEACTIVATE (GID_RF, OID 3)
```

So the data plane is plain: an APDU goes out as an **MT_DATA** packet
(`10 00 00 <len> <apdu>`) and the answer comes back the same way, with
the status word as the last two bytes.  The SELECT is the abbreviated
SeedKeeper AID `D2760000850101`; the card that was tested is not a
SeedKeeper, hence the `6A82`.

#### The port's own transport (2026-10-20)

`kruxport/kruxport/nfc.py` is the `nfc` module Krux
imports.  It replays the vendor's bring-up **byte for byte** -- the
payloads are generated from the captured trace, because two of them
were wrong when they were typed by hand (the LPTD one was three bytes
short, the routing one was missing its first byte; the chip accepted
both and then never polled).

What the port does now, all of it verified on the device:

* power cycle the chip and read the Core Reset notification ✓
* GET_BUILD_INFO, GET_PATCH_VERSION ✓ (the chip is `Dec 22 2012 ...
  20791B5`)
* CORE_RESET, CORE_INIT ✓
* the LPTD, start-up, antenna, poll, RF and protocol configurations ✓
  (all the responses come back with status 0)
* the two proprietary commands, the clock and the routing ✓
* the chip now answers with the same notifications the vendor's does
  (`10 61 07 01 00` twice) ✓

The one thing that was missing was the **wake line**: the chip stays
asleep (and silent) until it is asserted, and it has to be asserted
*before* the configuration, not after.  On Android the framework does
that when the screen comes on, which is why the chip is quiet with the
screen off.  With the wake asserted the port's chip behaves exactly
like the vendor's.

**Still open**: the chip does not report a tag with the card in the
position we have been using -- and neither does the vendor's own
stack, so it is the card's position, not the port.  The port's
discovery/activation/data plane is written but not yet seen to work
with a card.

#### The port talks to a real card (2026-10-20)

Three things had to be right, and each one cost a cycle:

1. **The routing and the protocol numbers are sent twice.**  The
   vendor's stack sends them, then deactivates the discovery, then
   sends them *again* -- and the chip only finds a tag after that.
   The trace of the ROM's own stack shows the tag's activation
   notification right after those two commands, and nowhere else.
   Skipping the second round: the chip never polls.

2. **The wake line has to be asserted before the configuration**, and
   not just before the polling.  With it deasserted the chip stays
   asleep and answers nothing at all.  (On Android the framework
   does this when the screen comes on -- which is why the chip is
   quiet with the screen off.  In TWRP the port does it itself.)

3. **`time.sleep()` takes seconds, not milliseconds.**  A settle time
   written as `SETTLE_MS = 300` was a five-minute pause, and every
   test looked like a hang.

With those fixed, the port -- on its own, in TWRP, with no vendor HAL
and no Android framework -- found a card, activated it and exchanged
an APDU with it:

```
=== attempt 4 ===
=== the card is there and activated ===
=== the card answered: data= sw=6a82
```

`6a82` is the same answer the ROM's own stack got from that card: it
is not a SeedKeeper, so the abbreviated AID `D2760000850101` is not
there.

The chip's poll is not reliable: a card that is in the field is seen
in some cycles and not in others (three cycles went by before the
fourth one found it), so `connect()` repeats the whole cycle until
its timeout runs out.

**The last fix, and it is the one that made it work every time**: the
discovery must not be restarted too often.  Asking the chip to
discover again every 300 ms *interrupts* the poll it is in the middle
of; with one discovery per second the card is found on the first
try.  The port's API is now exactly the one Krux's `seedkeeper.py`
expects:

```python
t = nfc.transport()
t.connect(40000)                  # -> connected: True
data, sw1, sw2 = t.transmit(apdu)  # -> data= , sw=6a82
t.close()
```

Two lessons worth keeping:

* the vendor's *own* trace is the reference: with the kernel logging
  every packet that goes through the `bcm2079x` driver, the ROM's
  NFC framework can be watched doing its work, and the port can be
  written against what it says;
* the card's position matters and the framework's own stack is the
  way to find it -- the NFC Reader app (com.github.muellerma.nfcreader)
  is installed on the device for that.

### The Krux firmware on the port (2026-10-20)

The firmware is **`/home/user/Develop/src/krux-sighash/krux`** (the
branch `unified-sighash_seedkeeper`; its `src/krux` is identical to
the krux-hammerhead tree's).  Its `vendor/embit` submodule is the
*sighash fork* of embit, which on MicroPython expects a native
`secp256k1` module -- the port supplies one.

What the port has so far, all verified on the device:

| piece | file | note |
|---|---|---|
| the display | `kruxport/lcd_bcm.c` | the framebuffer (`/dev/graphics/fb0`, 1080x1920, **stride 4352**), the 8x16 VGA font (from the kernel's own console font), the rotation/mirror/BGR in software, the colours in MaixPy's RGB565, and the channel offsets taken from the kernel (`FBIOGET_VSCREENINFO`) so they cannot be got wrong |
| the input devices | `kruxport/evdev_bcm.c` | `/dev/input/event1` (the touch) and the phone's keys; the events are read with a timeout so the firmware's loop does not block |
| the NFC chip | `kruxport/nfc_bcm2079x.c` + `kruxport/kruxport/nfc.py` | the transport, verified against a card |
| the I2C bus | `kruxport/machine_i2c_linux.c` | `machine.I2C`, MaixPy's API |
| the board | `kruxport/kruxport/board.py` | the hammerhead's config (the font, the touch, the pins) |
| the power | `kruxport/kruxport/pmu.py` | the battery, the USB and the backlight, over the power supply class and the backlight's sysfs |
| the touch and the buttons | `kruxport/kruxport/touch.py`, `buttons.py` | the firmware's own API, over evdev (the phone's keys instead of GPIO buttons) |
| the watchdog | `kruxport/kruxport/wdt.py` | nothing is armed (on Linux, closing the watchdog reboots the device) |
| the auto shutdown | `kruxport/kruxport/auto_shutdown.py` | the firmware's own, with the hardware timer replaced by the clock |
| the light | `kruxport/kruxport/light.py` | there is no LED to switch |
| the camera | `kruxport/kruxport/sensor.py` | a stand-in until the V4L2 camera path works |

The firmware now boots: it draws its splash, reads the settings,
checks the pin and reaches the **login page**.  The last thing it
wanted was MaixPy's `qrcode` module (the login page shows a QR code),
which the port still has to provide.

Four MicroPython limits the port has to work around, all found the
hard way:

* the C stack limit of the unix port is 40 KB and the firmware's
  import chain is deeper than that: the port raises it to 500 KB
  (`ports/unix/main.c`).
* MicroPython has no extended slices (`b[::-1]`): embit's pure-Python
  secp256k1 is patched (`kruxport/kruxport/secp256k1.py`).
* MicroPython's *dotted relative* import (`from .util.py_ripemd160
  import ...`) recurses: the three of them in embit are made absolute.
* MicroPython's `hashlib` has no `ripemd160` and no `new()`: embit
  falls back to its pure-Python implementation, and the port adds the
  cipher mode constants the firmware's settings name
  (`extmod/modcryptolib.c`, `MICROPY_PY_CRYPTOLIB_CONSTS`).

Two things are known broken and are not worked around yet:

* the classes the port adds to the `machine` module (the timer, the
  PWM, the watchdog) **segfault** MicroPython when they are
  constructed -- the `machine.I2C` class in another file is fine, so
  it is something in `machine_hw_linux.c`.  The firmware needs the
  timer (its auto shutdown) and the PWM (the backlight), so for now
  the port replaces the two firmware files that use them.
* the rootfs build: buildroot's musl toolchain was never built here
  (the "target-final only" build skips it), so the fragment uses the
  glibc toolchain that is present.  The rootfs only ships busybox and
  the hand-built static MicroPython, so the libc in it matters little.

## Space

/tmp is a tmpfs (RAM). Keep only what is in use: the kernel objects
(719 MB, rebuilt constantly -> /tmp/opencode/ko-cap) and the small
scratch. The rootfs build lives on the disk (rootfs/build).

---

# 2026-10-21 — three fixes, and the finding that the fault is in the kernel

## What was fixed

**1. The ION heap masks were wrong (`cam/ion.h`).**

```
ION_MASK_SYSTEM   0x00010000    <-- bit 16
ION_MASK_ADSP     0x00000004
ION_MASK_IOMMU    0x00000020
ION_MASK_QSECOM   0x00000080
```

`heap_id_mask` is a mask of heap **ids**, not of heap types.  The
system heap is **id 30** (`ION_SYSTEM_HEAP_ID` in
`include/linux/msm_ion.h`), so its mask is `1 << 30` = `0x40000000`.
Bit 16 is `ION_CP_WB_HEAP_ID` -- a **8660-only** heap -- and this
8974 does not have it, so every allocation failed with `ENOENT`:

```
[ion] ION_IOC_ALLOC (mask 0x10000): No such device
[client] dma-buf allocation failed
```

The heaps this kernel *does* register (from the device tree) are
**8, 22, 25, 27, 28 and 30**.  The camera's buffers work with the
system heap (30) and with the IOMMU heap (25); heap 8 (the CP MM
reserve) is too small for a 3280x2464 frame.

**2. The kernel oopsed when the CPP firmware could not be loaded.**

`msm_cpp_cfg()` -> `cpp_load_fw()` dereferenced the firmware
without checking it:

```c
rc = request_firmware(&fw, fw_name_bin, dev);
if (rc) { dev_err(...); }        /* and then: */
if (NULL != fw) ptr_bin = (uint32_t *)fw->data;
...
msm_cpp_write(fw->size, cpp_dev->base);   /* <-- fw is NULL here */
```

The call trace was `msm_camera_io_w+0x0`.  Our tree now checks the
firmware and gives up with a message instead (a genuine upstreamable
bug fix).

**3. The CPP firmware has to be in the kernel's firmware-loader
path, and it has to be version 1.2.0.**

The imaging server stages the blob in `/lib/firmware` and asks the
kernel to load it by name (`VIDIOC_MSM_CPP_LOAD_FIRMWARE`); the
kernel's `request_firmware()` looks for that name under
`/lib/firmware`, so the name has to be the bare file name.

The version is not a free choice: the kernel checks the message
against the firmware's stripe base,

```c
num_stripes = ((msg[12] >> 20) & 0x3FF) + ((msg[12] >> 10) & 0x3FF) + (msg[12] & 0x3FF);
if ((stripe_base + num_stripes*27 + 1) != new_frame->msg_len) ...
```

and the captured chromatrix has **5** stripes and **266** words:

```
130 + 27*5 + 1 = 266      <-- STRIPE_BASE_FW_1_2_0, i.e. firmware 1.2.0
140 + 27*5 + 1 = 276      <-- 1.4.0
464 + 27*5 + 1 = 600      <-- 1.6.0
```

So `cam/fw/cpp_firmware_v1_2_0.fw` is the one, and
without it the kernel says `invalid fw version 00000000` and then
`Invalid frame message`.

With the firmware loaded, the chromatrix is **accepted** (status 0)
and a frame arrives ("GOT A FRAME: index=2") -- still with
`bytesused=0`.

## The data type: the packets are on virtual channel 1

The CSID's LUT is indexed by the virtual channel, and the captured
long packet headers say which channel the packets are on:

```
unmapped long 0x2b010405   -> data type 0x2b, VC 1, 1029 words
mapped   long 0x1201040a   -> data type 0x12, VC 1, 1034 words
```

**Both data types arrive on VC 1.**  The vendor's HAL programs the
LUT with **VC 0 = 0x2b** and leaves VC 1 empty:

```
CSID0 LUT: vc0 0x0000122b vc1 0x00000000 vc2 0x00000000 vc3 0x00000000,
           cfg0 0x00000023 cfg1 0x00000013
```

so the packets are not mapped at all.  What the two configurations
do:

| the LUT | the packets | the CAMIF |
|---|---|---|
| VC0 = 0x2b, VC1 = 0x12 (what we had) | nothing mapped | error status **0xd0** (MIPI status fatal A+B) |
| VC0 = 0x12, VC1 = 0x12 | the 0x12 packets mapped | error status **0x0** -- the CAMIF is happy, but the frame is still empty |

Routing the ISP-IF at VC 1 (and mapping VC 1 = 0x2b) **crashes the
kernel** -- do not try it without a way back.

## The finding that matters: the vendor's camera has the same fault

The ROM's own camera app, running under **our** kernel, with the
**vendor's** HAL:

```
CSID0 LUT: vc0 0x0000122b vc1 0x00000000, cfg0 0x00000023 cfg1 0x00000013
CSID0 irq status 0x000002dd, packets 2363548, unmapped long 0x00000000,
                mapped long 0x00000000, short 0x0000011a, ecc 0, crc 0
```

**2,363,548 packets, none of them mapped, and exactly the same 282
"short" packets as our imaging server sees.**  The camera app starts,
the HAL configures the ISP and the CPP, the sensor streams -- and
nothing is mapped.

**So the fault is in our kernel, not in our imaging server.**  The
"282 short packets" is a constant in both, so it is not related to
the streaming at all.

How the vendor's camera was driven, without a human:

```
adb shell input keyevent 224                 # wake the screen
adb shell monkey -p com.google.android.GoogleCamera -c android.intent.category.LAUNCHER 1
adb shell input keyevent 115                 # the volume key is the shutter
adb shell am force-stop com.google.android.GoogleCamera   # the close prints the state
```

The CSID's state is printed when the camera is **closed**, so the
`force-stop` is what makes the numbers appear.

## Where the next step is

The one measurement that is still missing is the same one in the
**ROM's kernel** (where the camera works): the CSID's configuration
and the VFE's registers, taken the same way.  The candidates are

* the CSID's **VC mapping** -- the packets are on VC 1 and the LUT
  maps VC 0; something in the ROM's kernel (or its device tree)
  makes them land on VC 0;
* the CSID's **stream mux** and the "packet size";
* the **ISP-IF's routing** (which cid the VFE takes).

Our VFE register dump (for the comparison) is in the kernel log
when the frame ends: `DEBUG: VFE regs:`.

## 2026-10-28 — Stage 1 kernel build and V4L2 measurement

Kernel built from `linux` at `airgap/master-cleanup` (`95f4b0f38ce`) with the
camera profile, using the host ARM toolchain (`arm-linux-gnueabihf-gcc`), output
`/tmp/opencode/ko-cam`, image `artifacts/built/boot-cam.img`.  `CONFIG_DEVTMPFS`
was temporarily enabled (`=y`) for the build; restored to `=n` afterwards, so a
camera build must re-apply `sed -i 's/^CONFIG_DEVTMPFS=n/CONFIG_DEVTMPFS=y/'`.

Verified in `/tmp/opencode/ko-cam/.config`:

- `CONFIG_MSMB_CAMERA=y`, `CONFIG_IMX179=y`, `CONFIG_MSM_CSID=y`,
  `CONFIG_DEVTMPFS=y`, `CONFIG_NFC_BCM2079X=y`.

Boot verification (TWRP, uid 0, `/proc/version` contains `95f4b0f38ce`):

- `/dev/video0`, `/dev/video1`, `/dev/v4l-subdev3`, `/dev/v4l-subdev11`,
  `/dev/v4l-subdev13` all present (devtmpfs is working).

Measurement (binned IMX179 mode, 1640×1232, `PIX_VIEWFINDER`, no CPP,
`use_vendor_sensor_seq=0` so the mode table + binned settings land
`0x034c..0x034f = 1640/1232`):

- Sensor streams and is programmed correctly (register read-back matches).
- `STREAMON` succeeds, but no `DQBUF` ever completes; the client prints
  `no frame after 5s`.
- Kernel log: repeated `msm_ispif_read_irq_status: VFE0 pix0 overflow.`
  (~710 lines per 8 s run), then `msm_isp_close_node: halt timeout` and
  `msm_vb2_buf_cleanup: NULL stream` on teardown.
- No `planes[0].bytesused` measurement was possible because no buffer was
  dequeued; the frame is not merely empty, the VFE never completes a buffer.

Interpretation: the ISP-IF is delivering pixels (the overflow is an input-side
FIFO overrun in `msm_ispif`, i.e. data is arriving faster than the VFE40
consumes it), but the VFE does not drain them into the queued buffers.  That
is a kernel/VFE clock or configuration difference, not a userspace buffer
problem: the buffers are allocated on heap 30, imported, enqueued and
streamed, and the same code path that previously produced `GOT A FRAME`
under the raw `CAMIF_RAW` path now cannot even complete a processed frame.

Chosen path: **Path A (native V4L2 + `camd`)**.  Phase A is not fully closed
(the processed `PIX_VIEWFINDER` path is still blocked by VFE overflow), but
the `CAMIF_RAW` path delivers non-empty frames and is the path used by `camd`.

### 2026-10-29 — Stage 3 smoke test passed

`cam/camd.c` (daemon, derived from `imaging8.c`) runs on the device, captures
QVGA frames (centre-crop 1600×1200, 5×5 box-average to 320×240), and serves
them over TCP port 8765.  `krux/src/sensor.py` connects to camd and implements
the MaixPy-compatible API.  `krux/src/image.py` provides `width()`/`height()`.

Smoke test (10 iterations): all 10 frames are 320×240, `get_id()` returns
`0x179`, no `SensorError`, no oops in dmesg.

### 2026-10-29 — Stage 3 status

**Completed:**
- camd daemon: captures QVGA frames, serves over Unix socket (not TCP).
- sensor.py: MaixPy-compatible client of camd.
- _image native module: Image, width, height, to_bytes, from_nv12, get_statistics, find_qrcodes (quirc integrated).
- image.py wrapper: re-exports _image.Image with MaixPy-compatible stubs.
- lcd_bcm.c: lcd_display() accepts _image.Image (RGB565→32-bit).
- Smoke test: 10 frames 320×240, get_id() → 0x179, no SensorError.
- 100-frame stability: passed (mem_free stable).
- to_bytes(): 153,600 bytes (320×240×2 RGB565).
- get_statistics(): computes l_mean, l_stdev from Y channel (stdev fixed).
- find_qrcodes(): integrated with quirc (returns [] on blank).
- Unix socket: no TCP listener (air-gap compliant).
- CPP streaming: fixed with retry + 50ms delay (0 EAGAIN errors).

**Remaining for full acceptance:**
- QR decode on actual QR codes (needs bright lighting; image is dark, l_mean≈1-2).
- Scene-dependent entropy (l_stdev varies but image is dark).
- 50 reset/run/close cycles (not yet tested).
- 30-min long-run stability.
- Air-gap verification (boot air-gap kernel, check no new interface/socket/module).
- Auto-exposure: add __write_reg support in camd for dynamic exposure/gain.
- Deploy script: push cpp_firmware (done) and camd (done).

### 2026-10-28 — Stage 2 native POC measurement

Instrumented kernel (`/tmp/opencode/ko-camdbg`, CAMDBG prints in
`msm_isp_process_done_buf` and `msm_isp_buf_done`) was booted and the binned
`PIX_VIEWFINDER` path was run.  `dmesg | grep CAMDBG` returned **zero lines**:
neither `process_done_buf` nor `buf_done` is ever called.  Combined with the
`VFE0 pix0 overflow` flood, the failing stage for the *processed* path is:
**frame-done interrupt never reaches the buffer-completion path** (stream
start/update or WM IRQ mask), so no buffer is completed and
`planes[0].bytesused` cannot be measured.

The `CAMIF_RAW` path (CPP firmware v1.2.0, binned mode) was then run per
`plan-camera.md` §A3.  It **succeeds**: `GOT A FRAME: index=2`, the dequeued
buffer holds ~900 k non-zero bytes, and `/data/local/tmp/frame0.nv12` (2 020 480
bytes) was saved and pulled to `/tmp/opencode/frame0.nv12`.  No kernel oops and
no `camif error status 0xd0` in the log.

Stage 2 acceptance is therefore met on the raw path.  The processed
`PIX_VIEWFINDER` path remains blocked by the VFE overflow and is deferred; the
raw/CPP path is the one to carry into Stage 3.

> **Path note:** this document predates the source consolidation. The port
> modules are now in `krux/src/` and `micropython/ports/unix/`; `kruxport/`
> no longer exists.
