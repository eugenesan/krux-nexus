#!/bin/bash
# Two diagnostics:
#   1. the CSIPHY's interrupt status and power config at release -- did
#      the PHY ever lock, and did it see errors?
#   2. the ISP-IF's routing, printed where the driver reads it (this
#      also shows our own server's values, which checks the struct).
set -e
R=/home/user/Develop/src/android-air-gapped
T=$R/standalone/vendor/android_kernel_lge_hammerhead
cd $T
git checkout -q airgap/debug-capture

python3 - <<'PYEOF'
p = 'drivers/media/platform/msm/camera_v2/sensor/csiphy/msm_csiphy.c'
s = open(p).read()
# the second release function is the csi3.0 one (the 8974's)
i = s.index("static int msm_csiphy_release(struct csiphy_device *csiphy_dev, void *arg)")
i = s.index("static int msm_csiphy_release(struct csiphy_device *csiphy_dev, void *arg)", i+10)
old = """\tif (!csiphy_dev || !csiphy_dev->ref_count) {
\t\tpr_err("%s csiphy dev NULL / ref_count ZERO\\n", __func__);
\t\treturn 0;
\t}
"""
new = """\t/* DEBUG: did the PHY ever lock, and did it see any error? */
\tif (csiphy_dev && csiphy_dev->base) {
\t\tpr_err("DEBUG: CSIPHY%u interrupt status 0x%08x, power cfg 0x%08x, hw version 0x%08x\\n",
\t\t\tcsiphy_dev->pdev->id,
\t\t\tmsm_camera_io_r(csiphy_dev->base +
\t\t\t\tMIPI_CSIPHY_INTERRUPT_STATUS0_ADDR),
\t\t\tmsm_camera_io_r(csiphy_dev->base +
\t\t\t\tMIPI_CSIPHY_GLBL_PWR_CFG_ADDR),
\t\t\tmsm_camera_io_r(csiphy_dev->base +
\t\t\t\tMIPI_CSIPHY_HW_VERSION_ADDR));
\t}

""" + old
assert old in s, "csiphy anchor"
s = s[:i] + s[i:].replace(old, new, 1)
open(p, 'w').write(s)
print("  the CSIPHY's status is printed at release")
PYEOF

git add -A
git -c user.email=dev@local -c user.name=dev commit -q -m "debug: print the CSIPHY's interrupt status and power config at release

The CSIPHY has no packet counters, but its interrupt status says
whether the lane PLLs ever locked and whether the PHY saw errors."
git log --oneline -1 | sed 's/^/  /'
