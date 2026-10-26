#!/bin/bash
# Dump the ISP-IF's routing registers AFTER the start call: the enable
# bit, the CID mask (which virtual channels reach PIX0) and the crop.
set -e
R=/home/user/Develop/src/android-air-gapped
T=$R/standalone/vendor/android_kernel_lge_hammerhead
cd $T
git checkout -q airgap/debug-capture

python3 - <<'PYEOF'
p = 'drivers/media/platform/msm/camera_v2/ispif/msm_ispif.c'
s = open(p).read()
# remove the earlier (pre-start) dump
start = s.find("\t\t/* DEBUG: what did we put into the VFE's ISPIF block? */")
if start >= 0:
    end = s.index("\t\trc = msm_ispif_start_frame_boundary", start)
    s = s[:start] + s[end:]
    print("  the old dump removed")

old = """\tcase ISPIF_START_FRAME_BOUNDARY:
\t\trc = msm_ispif_start_frame_boundary(ispif, &pcdata->params);
"""
new = """\tcase ISPIF_START_FRAME_BOUNDARY:
\t\trc = msm_ispif_start_frame_boundary(ispif, &pcdata->params);
\t\t/* DEBUG: the routing, after the start */
\t\t{
\t\t\tint k;
\t\t\tpr_err("DEBUG: ISPIF CTRL_0 0x%08x, CID_MASK0 0x%08x, CROP0 0x%08x\\n",
\t\t\t\tmsm_camera_io_r(ispif->base + ISPIF_VFE_m_CTRL_0(0)),
\t\t\t\tmsm_camera_io_r(ispif->base +
\t\t\t\t\tISPIF_VFE_m_PIX_INTF_n_CID_MASK(0, 0)),
\t\t\t\tmsm_camera_io_r(ispif->base +
\t\t\t\t\tISPIF_VFE_m_PIX_INTF_n_CROP(0, 0)));
\t\t\tfor (k = 0; k < 24; k++)
\t\t\t\tpr_err("DEBUG: ISPIF reg 0x%03x = 0x%08x\\n",
\t\t\t\t\t0x240 + 4 * k,
\t\t\t\t\tmsm_camera_io_r(ispif->base + 0x240 + 4 * k));
\t\t}
"""
assert old in s, "anchor"
s = s.replace(old, new, 1)
open(p, 'w').write(s)
print("  the routing is dumped after the start")
PYEOF

git add -A
git -c user.email=dev@local -c user.name=dev commit -q -m "debug: dump the ISP-IF's routing registers after the start call

The enable bit, the CID mask (which virtual channels reach PIX0) and
the crop, plus the whole 0x240 region."
git log --oneline -1 | sed 's/^/  /'
