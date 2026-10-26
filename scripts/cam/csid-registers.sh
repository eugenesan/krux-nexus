#!/bin/bash
# Print the CSID's interrupt status and its packet registers when it is
# released: the status says why the packets were not mapped (unmapped
# virtual channel, unsupported data type, overflow, ...).
set -e
R=/home/user/Develop/src/android-air-gapped
T=$R/standalone/vendor/android_kernel_lge_hammerhead
cd $T
git checkout -q airgap/debug-capture

python3 - <<'PYEOF'
p = 'drivers/media/platform/msm/camera_v2/sensor/csid/msm_csid.c'
s = open(p).read()
# remove the file capture (it cannot work: the data is in kernel memory)
start = s.find("\t/* DEBUG: capture the configuration the camera HAL sends */")
if start >= 0:
    end = s.index("\trc = msm_csid_cid_lut(", start)
    s = s[:start] + s[end:]
    print("  the file capture removed")

old = """\t/* DEBUG: how much data ever reached this receiver? */
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
"""
new = """\t/* DEBUG: how much data ever reached this receiver, and why was
\t * it not mapped? */
\tif (csid_dev->base) {
\t\tpr_err("DEBUG: CSID%u irq status 0x%08x, packets %u, unmapped long 0x%08x, mapped long 0x%08x, short 0x%08x, ecc %u, crc %u\\n",
\t\t\tcsid_dev->pdev->id,
\t\t\tmsm_camera_io_r(csid_dev->base + CSID_IRQ_STATUS_ADDR),
\t\t\tmsm_camera_io_r(csid_dev->base +
\t\t\t\tCSID_STATS_TOTAL_PKTS_RCVD_ADDR),
\t\t\tmsm_camera_io_r(csid_dev->base +
\t\t\t\tCSID_CAPTURED_UNMAPPED_LONG_PKT_HDR_ADDR),
\t\t\tmsm_camera_io_r(csid_dev->base +
\t\t\t\tCSID_CAPTURED_MMAPPED_LONG_PKT_HDR_ADDR),
\t\t\tmsm_camera_io_r(csid_dev->base +
\t\t\t\tCSID_CAPTURED_SHORT_PKT_ADDR),
\t\t\tmsm_camera_io_r(csid_dev->base + CSID_STATS_ECC_ADDR),
\t\t\tmsm_camera_io_r(csid_dev->base + CSID_STATS_CRC_ADDR));
\t}
"""
assert old in s, "anchor"
s = s.replace(old, new, 1)
open(p, 'w').write(s)
print("  the CSID registers are printed at release")
PYEOF

git add -A
git -c user.email=dev@local -c user.name=dev commit -q -m "debug: print the CSID's interrupt status and packet registers at release

The status says why the packets were not mapped: an unmapped virtual
channel, an unsupported data type, an overflow."
git log --oneline -1 | sed 's/^/  /'
