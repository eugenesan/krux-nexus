#!/bin/bash
# Print the CSID's packet counters when it is released: if
# CSID_STATS_TOTAL_PKTS_RCVD is zero, no MIPI data ever reached the
# receiver, and the problem is on the sensor side of the link.
set -e
R=/home/user/Develop/src/android-air-gapped
T=$R/standalone/vendor/android_kernel_lge_hammerhead
cd $T
git checkout -q airgap/debug-capture

python3 - <<'PYEOF'
p = 'drivers/media/platform/msm/camera_v2/sensor/csid/msm_csid.c'
s = open(p).read()
old = """\tcsid_dev->base = NULL;
"""
# find the release function and add the counters just before the state change
i = s.index("static int msm_csid_release(struct csid_device *csid_dev)")
j = s.index("\n}", i)
body = s[i:j]
anchor = "\tcsid_dev->csid_state = CSID_POWER_DOWN;"
assert anchor in body, "anchor"
new = """\t/* DEBUG: how much data ever reached this receiver? */
\tif (csid_dev->base) {
\t\tpr_err("DEBUG: CSID%u packets received: %u, unmapped long pkts: %u, mapped long pkts: %u\\n",
\t\t\tcsid_dev->pdev->id,
\t\t\tmsm_camera_io_r(csid_dev->base +
\t\t\t\tCSID_STATS_TOTAL_PKTS_RCVD_ADDR),
\t\t\tmsm_camera_io_r(csid_dev->base +
\t\t\t\tCSID_CAPTURED_UNMAPPED_LONG_PKT_HDR_ADDR),
\t\t\tmsm_camera_io_r(csid_dev->base +
\t\t\t\tCSID_CAPTURED_MMAPPED_LONG_PKT_HDR_ADDR));
\t}
""" + anchor
body = body.replace(anchor, new, 1)
s = s[:i] + body + s[j:]
open(p, 'w').write(s)
print("  the CSID counters are printed at release")
PYEOF

git add -A
git -c user.email=dev@local -c user.name=dev commit -q -m "debug: print the CSID's packet counters when it is released

CSID_STATS_TOTAL_PKTS_RCVD says how many MIPI packets the receiver has
seen.  If it stays at zero, no data ever reached the CSID and the
sensor side of the link is where the problem is."
git log --oneline -1 | sed 's/^/  /'
