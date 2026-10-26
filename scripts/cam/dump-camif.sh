#!/bin/bash
# Read back the VFE's CAMIF registers after the ISP-IF has been
# configured: the enable bits, the geometry and the input select.
set -e
R=/home/user/Develop/src/android-air-gapped
T=$R/standalone/vendor/android_kernel_lge_hammerhead
cd $T
git checkout -q airgap/debug-capture

python3 - <<'PYEOF'
p = 'drivers/media/platform/msm/camera_v2/isp/msm_isp32.c'
s = open(p).read()
old = """\tval = msm_camera_io_r(vfe_dev->vfe_base + 0x1E4);
\tif (update_state == ENABLE_CAMIF) {
"""
new = """\tval = msm_camera_io_r(vfe_dev->vfe_base + 0x1E4);
\tif (update_state == ENABLE_CAMIF) {
"""
assert old in s, "anchor"
# print the CAMIF registers at the end of the function
old2 = """\t} else if (update_state == DISABLE_CAMIF_IMMEDIATELY) {"""
new2 = """\t\t/* DEBUG: the VFE's camera front end, as programmed */
\t\tpr_err("DEBUG: VFE camif: 0x1E0 0x%08x 0x1E4 0x%08x 0x1EC 0x%08x 0x1F0 0x%08x 0x1F4 0x%08x 0x6FC 0x%08x 0x14 0x%08x\\n",
\t\t\tmsm_camera_io_r(vfe_dev->vfe_base + 0x1E0),
\t\t\tmsm_camera_io_r(vfe_dev->vfe_base + 0x1E4),
\t\t\tmsm_camera_io_r(vfe_dev->vfe_base + 0x1EC),
\t\t\tmsm_camera_io_r(vfe_dev->vfe_base + 0x1F0),
\t\t\tmsm_camera_io_r(vfe_dev->vfe_base + 0x1F4),
\t\t\tmsm_camera_io_r(vfe_dev->vfe_base + 0x6FC),
\t\t\tmsm_camera_io_r(vfe_dev->vfe_base + 0x14));
\t} else if (update_state == DISABLE_CAMIF_IMMEDIATELY) {"""
assert old2 in s, "anchor2"
s = s.replace(old2, new2, 1)
open(p, 'w').write(s)
print("  the VFE's CAMIF registers are printed")
PYEOF

git add -A
git -c user.email=dev@local -c user.name=dev commit -q -m "debug: print the VFE's camera front end registers

The enable bits, the geometry and the input select, after the ISP-IF
has been configured: the last part of the path that has never been
read back."
git log --oneline -1 | sed 's/^/  /'
