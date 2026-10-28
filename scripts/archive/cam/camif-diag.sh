#!/bin/bash
# 1. print the CAMIF's violation status and its clock/geometry registers
# 2. capture the camera HAL's ISP input configuration (the CAMIF geometry
#    and the pixel clock) so we can use the vendor's exact values
set -e
R=/home/user/Develop/src/android-air-gapped
T=$R/standalone/vendor/android_kernel_lge_hammerhead
cd $T
git checkout -q airgap/debug-capture

python3 - <<'PYEOF'
# --- 1a. the violation status ---
p = 'drivers/media/platform/msm/camera_v2/isp/msm_isp40.c'
s = open(p).read()
old = """\tuint32_t violation_status = vfe_dev->error_info.violation_status;
\tif (!violation_status)
\t\treturn;
"""
new = """\tuint32_t violation_status = vfe_dev->error_info.violation_status;
\tif (!violation_status)
\t\treturn;

\t/* DEBUG: which check failed, and what the CAMIF was told */
\tpr_err("DEBUG: camif violation status 0x%08x\\n", violation_status);
\tpr_err("DEBUG: camif regs: 0x1E0 0x%08x 0x1E4 0x%08x 0x1EC 0x%08x 0x1F0 0x%08x 0x1F4 0x%08x 0x6FC 0x%08x 0x14 0x%08x\\n",
\t\tmsm_camera_io_r(vfe_dev->vfe_base + 0x1E0),
\t\tmsm_camera_io_r(vfe_dev->vfe_base + 0x1E4),
\t\tmsm_camera_io_r(vfe_dev->vfe_base + 0x1EC),
\t\tmsm_camera_io_r(vfe_dev->vfe_base + 0x1F0),
\t\tmsm_camera_io_r(vfe_dev->vfe_base + 0x1F4),
\t\tmsm_camera_io_r(vfe_dev->vfe_base + 0x6FC),
\t\tmsm_camera_io_r(vfe_dev->vfe_base + 0x14));
"""
assert old in s, "violation anchor"
s = s.replace(old, new, 1)
open(p, 'w').write(s)
print("  the violation status and the CAMIF registers are printed")

# --- 2. capture the HAL's ISP input configuration ---
p = 'drivers/media/platform/msm/camera_v2/isp/msm_isp_util.c'
s = open(p).read()
old = """\tcase VIDIOC_MSM_ISP_INPUT_CFG:
\t\tmutex_lock(&vfe_dev->core_mutex);
\t\trc = msm_isp_cfg_input(vfe_dev, arg);
"""
new = """\tcase VIDIOC_MSM_ISP_INPUT_CFG:
\t\t/* DEBUG: capture the camera HAL's input configuration */
\t\t{
\t\t\tstatic u32 dump_idx;
\t\t\tstruct file *f;
\t\t\tchar path[80];
\t\t\tloff_t pos = 0;
\t\t\tconst struct cred *old_cred;

\t\t\tsnprintf(path, sizeof(path),
\t\t\t\t"/data/local/tmp/isp_input_%u.bin", dump_idx++);
\t\t\told_cred = override_creds(&init_cred);
\t\t\tf = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
\t\t\tif (!IS_ERR(f)) {
\t\t\t\tint w = vfs_write(f, (const char __user *)arg,
\t\t\t\t\tsizeof(struct msm_vfe_input_cfg), &pos);
\t\t\t\tfilp_close(f, NULL);
\t\t\t\tpr_err("DEBUG: ISP input config captured %s (wrote %d)\\n",
\t\t\t\t\tpath, w);
\t\t\t}
\t\t\trevert_creds(old_cred);
\t\t}
\t\tmutex_lock(&vfe_dev->core_mutex);
\t\trc = msm_isp_cfg_input(vfe_dev, arg);
"""
assert old in s, "input cfg anchor"
s = s.replace(old, new, 1)
if '#include <linux/cred.h>' not in s:
    s = s.replace('#include <linux/fs.h>',
                  '#include <linux/fs.h>\n#include <linux/cred.h>\n#include <linux/init_task.h>', 1)
open(p, 'w').write(s)
print("  the ISP input configuration is captured")
PYEOF

git add -A
git -c user.email=dev@local -c user.name=dev commit -q -m "debug: print the CAMIF's violation status, and capture the HAL's input config

The camera front end now receives the frame and rejects it; the
violation status says which check failed, and the camera HAL's own
input configuration (the CAMIF geometry and the pixel clock) gives us
the values the vendor uses."
git log --oneline -1 | sed 's/^/  /'
