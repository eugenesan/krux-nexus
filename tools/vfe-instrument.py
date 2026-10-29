#!/usr/bin/env python3
"""Apply VFE overflow instrumentation patches to the kernel source tree.

Usage:
    python3 tools/vfe-instrument.py [--apply|--revert] [--linux-dir linux/]

Adds printk instrumentation to:
  - msm_isp40.c: VFE40 clock rates, hw version, WM IRQ mask
  - msm_ispif.c: ISP-IF input selection, overflow counter
"""

import argparse
import os
import sys

LINUX_DIR = "linux"

PATCHES = {
    "drivers/media/platform/msm/camera_v2/isp/msm_isp40.c": [
        # 1. Add instrumentation after clock enable in init_hardware
        (
            '\trc = msm_cam_clk_enable(&vfe_dev->pdev->dev, msm_vfe40_clk_info,\n'
            '\t\tvfe_dev->vfe_clk, ARRAY_SIZE(msm_vfe40_clk_info), 1);\n'
            '\tif (rc < 0)\n'
            '\t\tgoto clk_enable_failed;\n',
            '\trc = msm_cam_clk_enable(&vfe_dev->pdev->dev, msm_vfe40_clk_info,\n'
            '\t\tvfe_dev->vfe_clk, ARRAY_SIZE(msm_vfe40_clk_info), 1);\n'
            '\tif (rc < 0)\n'
            '\t\tgoto clk_enable_failed;\n'
            '\tpr_err("VFE40_DBG: hw_version=0x%x\\n", vfe_dev->vfe_hw_version);\n'
            '\tif (vfe_dev->vfe_clk[1])\n'
            '\t\tpr_err("VFE40_DBG: vfe_clk_src=%lu\\n", clk_get_rate(vfe_dev->vfe_clk[1]));\n'
            '\tif (vfe_dev->vfe_clk[3])\n'
            '\t\tpr_err("VFE40_DBG: camss_csi_vfe_clk=%lu\\n", clk_get_rate(vfe_dev->vfe_clk[3]));\n'
        ),
        # 2. Add instrumentation in axi_cfg_wm_irq_mask
        (
            '\tirq_mask = msm_camera_io_r(vfe_dev->vfe_base + 0x28);\n'
            '\tirq_mask |= 1 << (stream_info->wm[0] + 8);\n'
            '\tmsm_camera_io_w(irq_mask, vfe_dev->vfe_base + 0x28);\n',
            '\tirq_mask = msm_camera_io_r(vfe_dev->vfe_base + 0x28);\n'
            '\tpr_err("VFE40_DBG: WM_IRQ_MASK before=0x%x wm0=%d\\n", irq_mask, stream_info->wm[0]);\n'
            '\tirq_mask |= 1 << (stream_info->wm[0] + 8);\n'
            '\tmsm_camera_io_w(irq_mask, vfe_dev->vfe_base + 0x28);\n'
            '\tpr_err("VFE40_DBG: WM_IRQ_MASK after=0x%x\\n", irq_mask);\n'
        ),
    ],
    "drivers/media/platform/msm/camera_v2/ispif/msm_ispif.c": [
        # 1. Add instrumentation in sel_csid_core
        (
            '\tmsm_camera_io_w_mb(data, ispif->base +\n'
            '\t\tISPIF_VFE_m_INPUT_SEL(vfe_intf));\n',
            '\tmsm_camera_io_w_mb(data, ispif->base +\n'
            '\t\tISPIF_VFE_m_INPUT_SEL(vfe_intf));\n'
            '\tpr_err("ISPIF_DBG: INPUT_SEL vfe=%d intftype=%d csid=%d val=0x%x\\n",\n'
            '\t\tvfe_intf, intftype, csid, data);\n'
        ),
    ],
}

def apply_patch(filepath, old, new):
    with open(filepath, 'r') as f:
        content = f.read()
    if old not in content:
        print(f"  WARNING: pattern not found in {filepath}")
        return False
    content = content.replace(old, new, 1)
    with open(filepath, 'w') as f:
        f.write(content)
    return True

def revert_patch(filepath, old, new):
    with open(filepath, 'r') as f:
        content = f.read()
    if new not in content:
        print(f"  WARNING: patched pattern not found in {filepath}")
        return False
    content = content.replace(new, old, 1)
    with open(filepath, 'w') as f:
        f.write(content)
    return True

def main():
    parser = argparse.ArgumentParser(description="Apply/revert VFE instrumentation")
    parser.add_argument("--apply", action="store_true", help="Apply instrumentation")
    parser.add_argument("--revert", action="store_true", help="Revert instrumentation")
    parser.add_argument("--linux-dir", default="linux", help="Path to linux submodule")
    args = parser.parse_args()

    if not args.apply and not args.revert:
        print("Specify --apply or --revert")
        sys.exit(1)

    linux_dir = args.linux_dir
    if not os.path.isdir(linux_dir):
        print(f"Error: {linux_dir} not found")
        sys.exit(1)

    for relpath, replacements in PATCHES.items():
        filepath = os.path.join(linux_dir, relpath)
        if not os.path.isfile(filepath):
            print(f"Error: {filepath} not found")
            sys.exit(1)
        print(f"Processing {relpath}...")
        for old, new in replacements:
            if args.apply:
                if apply_patch(filepath, old, new):
                    print(f"  Applied patch")
                else:
                    print(f"  FAILED to apply patch")
            else:
                if revert_patch(filepath, old, new):
                    print(f"  Reverted patch")
                else:
                    print(f"  FAILED to revert patch")

if __name__ == "__main__":
    main()
EOF
chmod +x tools/vfe-instrument.py
echo "created"