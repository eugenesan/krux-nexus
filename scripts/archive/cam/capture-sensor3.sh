#!/bin/bash
# Capture both:
#  - the register ARRAY of every CFG_WRITE_I2C_ARRAY (sensor_cfg_*_n*.bin)
#  - the header of every sensor ioctl (sensorio_*_t*_s*.bin)
# so that our imaging server can replay the vendor's exact sequence.
set -e
R=/home/user/Develop/src/android-air-gapped
T=$R/standalone/vendor/android_kernel_lge_hammerhead
cd $T
git checkout -q airgap/debug-capture

python3 - <<'PYEOF'
p = 'drivers/media/platform/msm/camera_v2/sensor/msm_sensor.c'
s = open(p).read()

# the generic capture: rename its files so they do not collide
s = s.replace('"/data/local/tmp/sensor_cfg_%u_t%d_s%u.bin"',
              '"/data/local/tmp/sensorio_%u_t%d_s%u.bin"')
s = s.replace('DEBUG: sensor cfg captured %s (wrote %d)',
              'DEBUG: sensor ioctl captured %s (wrote %d)')

# and add the array capture back, in the CFG_WRITE_I2C_ARRAY case
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

\t\t/* DEBUG: capture the register array itself */
\t\t{
\t\t\tstatic u32 dump_idx;
\t\t\tstruct file *f;
\t\t\tchar path[80];
\t\t\tloff_t pos = 0;
\t\t\tconst struct cred *old_cred;

\t\t\tsnprintf(path, sizeof(path),
\t\t\t\t"/data/local/tmp/sensor_cfg_%u_n%d.bin",
\t\t\t\tdump_idx++, conf_array.size);
\t\t\told_cred = override_creds(&init_cred);
\t\t\tf = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
\t\t\tif (!IS_ERR(f)) {
\t\t\t\tint w = vfs_write(f,
\t\t\t\t\t(const char __user *)conf_array.reg_setting,
\t\t\t\t\tsizeof(struct msm_camera_i2c_reg_array) *
\t\t\t\t\t\tconf_array.size,
\t\t\t\t\t&pos);
\t\t\t\tfilp_close(f, NULL);
\t\t\t\tpr_err("DEBUG: sensor array captured %s (%d regs, wrote %d)\\n",
\t\t\t\t\tpath, conf_array.size, w);
\t\t\t}
\t\t\trevert_creds(old_cred);
\t\t}
"""
assert old in s, "array anchor"
s = s.replace(old, new, 1)
open(p, 'w').write(s)
print("  both captures in place")
PYEOF

git add -A
git -c user.email=dev@local -c user.name=dev commit -q -m "debug: capture the sensor's register arrays as well as the ioctl headers"
git log --oneline -1 | sed 's/^/  /'
