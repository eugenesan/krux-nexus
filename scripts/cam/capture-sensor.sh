#!/bin/bash
# Capture the sensor register writes the vendor camera HAL sends us
# (CFG_WRITE_I2C_ARRAY), so we can replay the vendor's exact sequence
# instead of guessing the order.
set -e
R=/home/user/Develop/src/android-air-gapped
T=$R/standalone/vendor/android_kernel_lge_hammerhead
cd $T
git checkout -q airgap/debug-capture

python3 - <<'PY'
p = 'drivers/media/platform/msm/camera_v2/sensor/msm_sensor.c'
s = open(p).read()
old = """\t\tif (copy_from_user(&conf_array,
\t\t\t(void *)cdata->cfg.setting,
\t\t\tsizeof(struct msm_camera_i2c_reg_setting))) {
\t\t\tpr_err("%s:%d failed\\n", __func__, __LINE__);
\t\t\trc = -EFAULT;
\t\t\tbreak;
\t\t}
"""
new = """\t\tif (copy_from_user(&conf_array,
\t\t\t(void *)cdata->cfg.setting,
\t\t\tsizeof(struct msm_camera_i2c_reg_setting))) {
\t\t\tpr_err("%s:%d failed\\n", __func__, __LINE__);
\t\t\trc = -EFAULT;
\t\t\tbreak;
\t\t}

\t\t/* DEBUG: capture the register array the camera HAL sends
\t\t * us, so that our own imaging server can replay exactly
\t\t * what the vendor sends (and in what order). */
\t\t{
\t\t\tstatic u32 dump_idx;
\t\t\tstruct file *f;
\t\t\tchar path[80];
\t\t\tloff_t pos = 0;
\t\t\tconst struct cred *old_cred;
\t\t\tint n = conf_array.size;

\t\t\tsnprintf(path, sizeof(path),
\t\t\t\t"/data/local/tmp/sensor_cfg_%u_t%d_n%d.bin",
\t\t\t\tdump_idx++, cdata->cfgtype, n);
\t\t\told_cred = override_creds(&init_cred);
\t\t\tf = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
\t\t\tif (!IS_ERR(f)) {
\t\t\t\tint w = vfs_write(f,
\t\t\t\t\t(const char __user *)conf_array.reg_setting,
\t\t\t\t\tsizeof(struct msm_camera_i2c_reg_array) * n,
\t\t\t\t\t&pos);
\t\t\t\tfilp_close(f, NULL);
\t\t\t\tpr_err("DEBUG: sensor config captured %s (%d regs, wrote %d)\\n",
\t\t\t\t\tpath, n, w);
\t\t\t}
\t\t\trevert_creds(old_cred);
\t\t}
"""
assert old in s, "anchor"
s = s.replace(old, new, 1)
if '#include <linux/cred.h>' not in s:
    s = s.replace('#include <linux/fs.h>', '#include <linux/fs.h>\n#include <linux/cred.h>\n#include <linux/init_task.h>', 1)
open(p, 'w').write(s)
print("  sensor capture added")
PY

grep -n '#include <linux/fs.h>' drivers/media/platform/msm/camera_v2/sensor/msm_sensor.c | head -2 | sed 's/^/  /'
git add -A
git -c user.email=dev@local -c user.name=dev commit -q -m "debug: capture the sensor register writes the camera HAL sends

Same trick as the ISP and CPP captures: the register array the HAL
writes with CFG_WRITE_I2C_ARRAY is written out to
/data/local/tmp/sensor_cfg_<n>_t<cfgtype>_n<count>.bin, so that our
own imaging server can replay the vendor's exact sequence -- including
the order, which matters for a sensor's mode change -- instead of
guessing it."
git log --oneline -1 | sed 's/^/  /'
