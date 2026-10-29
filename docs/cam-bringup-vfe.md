# Camera bring-up — VFE overflow fix

Paste this into a fresh session.  This is a deep-dive task that requires
both source analysis (no device needed) and device testing (device required).

## Goal

Fix `msm_ispif_read_irq_status: VFE0 pix0 overflow.` so the processed
`PIX_VIEWFINDER` path delivers frames, enabling a faster, simpler capture
path in `camd` that does not depend on the CPP firmware blob.

## Background

### What is VFE0 pix0 overflow?

The VFE40 (Video Front End) is the MSM8974's ISP.  In the `PIX_VIEWFINDER`
path, the camera sensor feeds raw pixels through the CSID → ISP-IF → VFE40
pipeline.  The VFE40 applies image processing and writes the result into a
queued buffer.  The ISP-IF sits between CSID and VFE40 and has an input FIFO.

When the ISP-IF delivers pixels faster than the VFE40 drains them, the input
FIFO overflows and the `PIX_INTF_0_OVERFLOW_IRQ` bit is set in
`ISPIF_VFE_m_IRQ_STATUS_0`.  The kernel logs:

```
msm_ispif_read_irq_status: VFE0 pix0 overflow.
```

~710 lines per 8-second run.  No buffer ever completes (`DQBUF` never
returns), `msm_isp_process_done_buf` and `msm_isp_buf_done` are never called,
and `msm_isp_close_node: halt timeout` fires on teardown.

### Why it happens (hypothesis)

The VFE40 clock is set to 266.67 MHz (`vfe_clk_src` in
`msm_vfe40_clk_info[]`).  The ISP-IF input FIFO has a fixed depth.  If the
sensor's pixel rate exceeds the VFE40's drain rate at this clock, the FIFO
overflows continuously.  Possible causes:

1. **VFE40 clock too low** — the air-gap kernel may not be scaling the VFE40
   clock correctly, or the clock rate is insufficient for the binned 1640×1232
   mode at 30 fps.
2. **ISP-IF input not configured** — wrong CSID, wrong virtual channel, or
   wrong data type causes the ISP-IF to misinterpret the pixel stream.
3. **VFE hardware version mismatch** — the driver checks `vfe_hw_version`
   against `VFE40_8974V1_VERSION` / `VFE40_8974V2_VERSION` / etc.  If the
   Nexus 5's VFE is a different version than expected, the wrong init path
   (QOS, VBIF) is taken.
4. **Bandwidth manager** — `msm_bus_scale` may not be granting enough bus
   bandwidth for the VFE40's write master, causing stalls.

### Key source files

| File | Role |
|---|---|
| `linux/drivers/media/platform/msm/camera_v2/isp/msm_isp40.c` | VFE40 driver: clock table, QOS, VBIF init, WM IRQ mask |
| `linux/drivers/media/platform/msm/camera_v2/ispif/msm_ispif.c` | ISP-IF driver: input selection, crop, CID mask, overflow detection |
| `linux/drivers/media/platform/msm/camera_v2/isp/msm_isp_util.c` | Bandwidth manager (`msm_bus_scale`) |
| `linux/arch/arm/boot/dts/msm8974-camera.dtsi` | Device tree: VFE/ISP-IF registers, CSID |
| `linux/arch/arm/mach-msm/clock-8974.c` | Clock definitions: `vfe_clk_src` at 266.67 MHz |

## Tooling

The following tools are provided to streamline the VFE overflow diagnosis:

| Tool | Path | Purpose |
|---|---|---|
| Instrumentation patcher | `tools/vfe-instrument.py` | Applies/reverts printk instrumentation to kernel source |
| Kernel builder | `scripts/cam/build-vfe-kernel.sh` | Builds instrumented camera kernel (handles devtmpfs toggle) |
| Test runner | `scripts/cam/test-vfe.sh` | Boots kernel, runs PIX_VIEWFINDER, captures dmesg |
| Param extractor | `scripts/cam/compare-vfe.sh` | Extracts VFE params from dmesg for comparison |

### Quick start

```sh
# 1. Build instrumented kernel
bash scripts/cam/build-vfe-kernel.sh /tmp/opencode/ko-vfe

# 2. Pack boot image
rm -rf /tmp/opencode/bootvfe && mkdir -p /tmp/opencode/bootvfe
python3 rootfs/bootimg.py unpack artifacts/built/twrp-3.7.0_9-HH.R.17-MADV_WIPEONFORK.img /tmp/opencode/bootvfe
cp /tmp/opencode/ko-vfe/arch/arm/boot/zImage-dtb /tmp/opencode/bootvfe/kernel
python3 rootfs/bootimg.py pack /tmp/opencode/bootvfe /tmp/opencode/bootvfe/ramdisk /tmp/opencode/boot-vfe.img

# 3. Run test and capture dmesg
bash scripts/cam/test-vfe.sh /tmp/opencode/boot-vfe.img

# 4. Extract params
adb shell 'dmesg' > /tmp/opencode/dmesg-vfe.txt
bash scripts/cam/compare-vfe.sh /tmp/opencode/dmesg-vfe.txt
```

### Applying instrumentation manually

If you need to modify the instrumentation:
```sh
# Apply
python3 tools/vfe-instrument.py --apply

# Revert (restore clean source)
python3 tools/vfe-instrument.py --revert
```

## Source analysis findings

### 1. VFE40 clock configuration

In `msm_isp40.c` line 85-92:

```c
static struct msm_cam_clk_info msm_vfe40_clk_info[] = {
    {"camss_top_ahb_clk", -1},
    {"vfe_clk_src", 266670000},    // 266.67 MHz
    {"camss_vfe_vfe_clk", -1},
    {"camss_csi_vfe_clk", -1},
    {"iface_clk", -1},
    {"bus_clk", -1},
};
```

The VFE40 core clock (`vfe_clk_src`) is hardcoded to 266.67 MHz.  The
`-1` entries mean "use default rate" (inherited from clock tree).  The
`camss_csi_vfe_clk` is the ISP-IF input clock — if this is too slow, the
ISP-IF may not keep up with the CSID output.

### 2. VFE hardware version detection

The driver selects QOS and VBIF init based on `vfe_hw_version`:

- `VFE40_8974V1_VERSION` → 8974 V1 QOS/VBIF
- `VFE40_8974V2_VERSION` / `VFE40_8974V3_VERSION` → 8974 V2/V3 QOS/VBIF
- `VFE40_8x26_VERSION` / `VFE40_8x26V2_VERSION` → 8x26 QOS/VBIF

The Nexus 5 uses MSM8974 (V1 or V2 depending on revision).  If the version
detection returns the wrong value, the wrong QOS/VBIF path is taken, causing
the overflow.

### 3. ISP-IF input selection

`msm_ispif_sel_csid_core()` writes to `ISPIF_VFE_m_INPUT_SEL` to select
which CSID feeds the VFE's PIX0 interface.  The Nexus 5 has 2 VFEs (VFE0
and VFE1) and 1 CSID.  The input selection must match the CSID index used
by the sensor driver.

### 4. WM (write master) IRQ mask

`msm_vfe40_axi_cfg_wm_irq_mask()` sets bit `(wm[0] + 8)` in VFE register
0x28.  This enables the WM (write master) done interrupt for the output
buffer.  If this bit is not set, the VFE never signals buffer completion
even if it processes the frame.

### 5. Bandwidth manager

`msm_isp_init_bandwidth_mgr()` registers a bus scale client.  The VFE40
needs sufficient bus bandwidth to write output frames to memory.  If the
bandwidth request is too low, writes stall and the input FIFO overflows.

## Tasks

### Task 1 — Add VFE40 instrumentation (no device needed)

Use the provided tooling:
```sh
python3 tools/vfe-instrument.py --apply
```

This adds printk instrumentation to `msm_isp40.c` and `msm_ispif.c` to capture:

1. **VFE40 clock rates** at stream start:
   ```c
   pr_err("VFE40: vfe_clk_src=%lu, camss_csi_vfe_clk=%lu\n",
       clk_get_rate(vfe_dev->vfe_clk[1]),  // vfe_clk_src
       clk_get_rate(vfe_dev->vfe_clk[3])); // camss_csi_vfe_clk
   ```

2. **VFE hardware version**:
   ```c
   pr_err("VFE40: hw_version=0x%x\n", vfe_dev->vfe_hw_version);
   ```

3. **ISP-IF input selection**:
   ```c
   pr_err("ISPIF: INPUT_SEL=0x%x\n",
       msm_camera_io_r(ispif->base + ISPIF_VFE_m_INPUT_SEL(0)));
   ```

4. **WM IRQ mask** at stream start:
   ```c
   pr_err("VFE40: WM_IRQ_MASK=0x%x\n",
       msm_camera_io_r(vfe_dev->vfe_base + 0x28));
   ```

5. **Bandwidth request**:
   ```c
   pr_err("VFE40: bus_ab=%llu, bus_ib=%llu\n", ab, ib);
   ```

### Task 2 — Build and boot instrumented kernel (device needed)

Use the provided build script (handles instrumentation + devtmpfs toggle):
```sh
bash scripts/cam/build-vfe-kernel.sh /tmp/opencode/ko-vfe
```

Then pack and boot:
```sh
rm -rf /tmp/opencode/bootvfe && mkdir -p /tmp/opencode/bootvfe
python3 rootfs/bootimg.py unpack artifacts/built/twrp-3.7.0_9-HH.R.17-MADV_WIPEONFORK.img /tmp/opencode/bootvfe
cp /tmp/opencode/ko-vfe/arch/arm/boot/zImage-dtb /tmp/opencode/bootvfe/kernel
python3 rootfs/bootimg.py pack /tmp/opencode/bootvfe /tmp/opencode/bootvfe/ramdisk /tmp/opencode/boot-vfe.img

adb reboot bootloader
fastboot boot /tmp/opencode/boot-vfe.img
```

### Task 3 — Run PIX_VIEWFINDER and capture dmesg (device needed)

Use the provided test script:
```sh
bash scripts/cam/test-vfe.sh /tmp/opencode/boot-vfe.img
```

Or manually:
```sh
# Start imaging with PIX_VIEWFINDER
adb shell 'cd /data/local/tmp && timeout 8 ./imaging-cam 1640 1232 0x40000000 rear 0 - 1 binned cfg nocycle 0 0x2b 0 0 300 0 0 0 0x12 0 1 0xffffffff 0 0 0 1 0' 2>&1 | tail -20 || true

# Capture dmesg to file
adb shell 'dmesg' > /tmp/opencode/dmesg-vfe.txt

# Extract params
bash scripts/cam/compare-vfe.sh /tmp/opencode/dmesg-vfe.txt
```

Look for:
- `VFE40: hw_version=0x...` — compare against expected (0x4002 for V2?)
- `VFE40: vfe_clk_src=...` — is it 266670000?
- `VFE40: camss_csi_vfe_clk=...` — is it sufficient?
- `ISPIF: INPUT_SEL=0x...` — does it match CSID index?
- `VFE40: WM_IRQ_MASK=0x...` — is bit (wm+8) set?
- `VFE40: bus_ab=..., bus_ib=...` — is bandwidth sufficient?

### Task 4 — Compare against ROM kernel (device needed)

Boot the stock ROM kernel (or extract its dmesg from `/proc/last_kmsg` if
available) and capture the same instrumentation.  Compare:

| Parameter | ROM kernel | Air-gap kernel | Match? |
|---|---|---|---|
| VFE hw_version | ? | ? | |
| vfe_clk_src | ? | 266670000 | |
| camss_csi_vfe_clk | ? | ? | |
| ISP-IF INPUT_SEL | ? | ? | |
| WM_IRQ_MASK | ? | ? | |
| bus_ab / bus_ib | ? | ? | |

### Task 5 — Apply fixes based on findings

Based on the comparison, apply one or more of:

**Fix A: Boost VFE40 clock**
If `vfe_clk_src` is lower than ROM, increase it:
```c
{"vfe_clk_src", 320000000},  // try 320 MHz
```

**Fix B: Fix ISP-IF input selection**
If `INPUT_SEL` doesn't match CSID index, fix the CSID index passed to
`msm_ispif_sel_csid_core()`.

**Fix C: Fix VFE hardware version**
If `hw_version` is wrong, the QOS/VBIF init path is wrong.  Fix the
version detection in `msm_isp40.c` or add the correct version to the
switch statement.

**Fix D: Fix WM IRQ mask**
If `WM_IRQ_MASK` doesn't have bit (wm+8) set, the `cfg_wm_irq_mask`
function isn't being called.  Check the stream setup path.

**Fix E: Fix bandwidth request**
If `bus_ab`/`bus_ib` are too low, increase the bandwidth request in
`msm_isp_update_bandwidth()`.

### Task 6 — Verify fix (device needed)

After applying a fix:
1. Rebuild kernel
2. Boot on device
3. Run PIX_VIEWFINDER imaging
4. Check dmesg for `VFE0 pix0 overflow` — should be 0
5. Check for `GOT A FRAME` in imaging output
6. Run 100-frame stability test

## Acceptance criteria

- `VFE0 pix0 overflow` count = 0 during 8-second run
- `GOT A FRAME` with non-zero bytes on PIX_VIEWFINDER path
- `msm_isp_process_done_buf` or `msm_isp_buf_done` called (instrumentation)
- No `halt timeout` or `NULL stream` on teardown
- 100-frame stability (no oops, no reboot)

## Notes

- The `CAMIF_RAW` path is the working fallback.  If VFE fix fails, `camd`
  continues using `CAMIF_RAW` + CPP v1.2.0.
- The VFE fix is an optimization, not a blocker for Stage 4 acceptance.
- Device is unstable under sustained load; use ≤ 20 frame runs.
- All kernel changes go in `linux/` submodule (branch `airgap/cam-ksu` or
  a new branch).  Main repo pointer stays on `airgap/master-cleanup` unless
  the fix is committed.
EOF
echo "written"