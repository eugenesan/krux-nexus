#!/bin/bash
# Capture the CSID configuration the vendor camera HAL sends: the
# parameters (lanes, assignment, the VC LUT) that decide which virtual
# channel and data type the receiver accepts.
set -e
R=/home/user/Develop/src/android-air-gapped
T=$R/standalone/vendor/android_kernel_lge_hammerhead
cd $T
git checkout -q airgap/debug-capture

python3 - <<'PYEOF'
p = 'drivers/media/platform/msm/camera_v2/sensor/csid/msm_csid.c'
s = open(p).read()
old = """\tmsm_camera_io_w(0x1, csidbase + MIPI_CSIPHY_GLBL_T_INIT_CFG0_ADDR);
"""
# not in this file; use the CSID config function instead
old = """\trc = msm_csid_cid_lut(&csid_params->lut_params, csidbase);
\tif (rc < 0)
\t\treturn rc;
"""
new = """\t/* DEBUG: capture the configuration the camera HAL sends */
\t{
\t\tstatic u32 dump_idx;
\t\tstruct file *f;
\t\tchar path[80];
\t\tloff_t pos = 0;
\t\tconst struct cred *old_cred;
\t\tint i;

\t\tsnprintf(path, sizeof(path),
\t\t\t"/data/local/tmp/csid_cfg_%u.bin", dump_idx++);
\t\told_cred = override_creds(&init_cred);
\t\tf = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
\t\tif (!IS_ERR(f)) {
\t\t\tstruct msm_camera_csid_vc_cfg vc_dump[4];
\t\t\tint w;

\t\t\tmemset(vc_dump, 0, sizeof(vc_dump));
\t\t\tfor (i = 0; i < csid_params->lut_params.num_cid && i < 4; i++)
\t\t\t\tvc_dump[i] = *csid_params->lut_params.vc_cfg[i];
\t\t\tw = vfs_write(f, (const char __user *)csid_params,
\t\t\t\tsizeof(*csid_params), &pos);
\t\t\tw += vfs_write(f, (const char __user *)vc_dump,
\t\t\t\tsizeof(vc_dump), &pos);
\t\t\tfilp_close(f, NULL);
\t\t\tpr_err("DEBUG: CSID config captured %s (lanes %d, assign 0x%x, phy %d, %d vcs; wrote %d)\\n",
\t\t\t\tpath, csid_params->lane_cnt, csid_params->lane_assign,
\t\t\t\tcsid_params->phy_sel,
\t\t\t\tcsid_params->lut_params.num_cid, w);
\t\t}
\t\trevert_creds(old_cred);
\t}

""" + old
assert old in s, "anchor"
s = s.replace(old, new, 1)
if '#include <linux/cred.h>' not in s:
    s = s.replace('#include <linux/irqreturn.h>',
                  '#include <linux/irqreturn.h>\n#include <linux/fs.h>\n#include <linux/cred.h>\n#include <linux/init_task.h>', 1)
open(p, 'w').write(s)
print("  the CSID config capture is in")
PYEOF

git add -A
git -c user.email=dev@local -c user.name=dev commit -q -m "debug: capture the CSID configuration the camera HAL sends

The virtual channel and the data type decide whether the receiver
accepts a packet; the HAL knows both, we were guessing."
git log --oneline -1 | sed 's/^/  /'
