#!/bin/bash
# Dump what the ISP-IF wrote into the VFE's ISPIF block, and what the
# CSID's VC LUT holds, so the two can be compared with the ISP's CAMIF
# geometry and the sensor's data type.
set -e
R=/home/user/Develop/src/android-air-gapped
T=$R/standalone/vendor/android_kernel_lge_hammerhead
cd $T
git checkout -q airgap/debug-capture

python3 - <<'PYEOF'
# --- the ISP-IF's own registers ---
p = 'drivers/media/platform/msm/camera_v2/ispif/msm_ispif.c'
s = open(p).read()
old = """\tcase ISPIF_START_FRAME_BOUNDARY:
\t\trc = msm_ispif_start_frame_boundary(ispif, &pcdata->params);
"""
new = """\tcase ISPIF_START_FRAME_BOUNDARY:
\t\t/* DEBUG: what did we put into the VFE's ISPIF block? */
\t\t{
\t\t\tint k;
\t\t\tfor (k = 0; k < 16; k++)
\t\t\t\tpr_err("DEBUG: ISPIF reg 0x%03x = 0x%08x\\n",
\t\t\t\t\t0x200 + 4 * k,
\t\t\t\t\tmsm_camera_io_r(ispif->base + 0x200 + 4 * k));
\t\t}
\t\trc = msm_ispif_start_frame_boundary(ispif, &pcdata->params);
"""
assert old in s, "ispif anchor"
s = s.replace(old, new, 1)
open(p, 'w').write(s)
print("  the ISPIF's registers are dumped")

# --- the CSID's VC LUT ---
p = 'drivers/media/platform/msm/camera_v2/sensor/csid/msm_csid.c'
s = open(p).read()
old = """\t/* DEBUG: how much data ever reached this receiver, and why was
\t * it not mapped? */
"""
new = """\t/* DEBUG: the VC LUT the camera HAL's configuration produced */
\tif (csid_dev->base) {
\t\tpr_err("DEBUG: CSID%u LUT: vc0 0x%08x vc1 0x%08x vc2 0x%08x vc3 0x%08x, cfg0 0x%08x cfg1 0x%08x\\n",
\t\t\tcsid_dev->pdev->id,
\t\t\tmsm_camera_io_r(csid_dev->base + CSID_CID_LUT_VC_0_ADDR),
\t\t\tmsm_camera_io_r(csid_dev->base + CSID_CID_LUT_VC_1_ADDR),
\t\t\tmsm_camera_io_r(csid_dev->base + CSID_CID_LUT_VC_2_ADDR),
\t\t\tmsm_camera_io_r(csid_dev->base + CSID_CID_LUT_VC_3_ADDR),
\t\t\tmsm_camera_io_r(csid_dev->base + CSID_CID_n_CFG_ADDR),
\t\t\tmsm_camera_io_r(csid_dev->base + CSID_CID_n_CFG_ADDR + 4));
\t}

""" + old
assert old in s, "csid anchor"
s = s.replace(old, new, 1)
open(p, 'w').write(s)
print("  the CSID's VC LUT is dumped")
PYEOF

git add -A
git -c user.email=dev@local -c user.name=dev commit -q -m "debug: dump the ISPIF's registers and the CSID's VC LUT

The ISPIF is a bridge inside the VFE: it programs the VFE's own ISPIF
block, and nobody has ever read back what it wrote."
git log --oneline -1 | sed 's/^/  /'
