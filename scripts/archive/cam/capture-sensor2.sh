#!/bin/bash
# Capture EVERY sensor config ioctl the camera HAL makes (not just
# CFG_WRITE_I2C_ARRAY): the payload and its size, so that our imaging
# server can replay the vendor's exact sequence.
set -e
R=/home/user/Develop/src/android-air-gapped
T=$R/standalone/vendor/android_kernel_lge_hammerhead
cd $T
git checkout -q airgap/debug-capture

python3 - <<'PYEOF'
p = 'drivers/media/platform/msm/camera_v2/sensor/msm_sensor.c'
s = open(p).read()

# 1. remove the old, CFG_WRITE_I2C_ARRAY-specific capture
marker = "\t\t/* DEBUG: capture the register array the camera HAL sends"
if marker in s:
    start = s.index(marker)
    end = s.index("\t\trevert_creds(old_cred);\n\t\t}\n", start) + len("\t\trevert_creds(old_cred);\n\t\t}\n")
    s = s[:start] + s[end:]
    print("  removed the old capture")

# 2. the generic helper
helper = """/*
 * DEBUG: capture every sensor configuration the camera HAL sends.
 *
 * The payload of each ioctl (struct sensorb_cfg_data::cfg) is written
 * out to /data/local/tmp/sensor_cfg_<n>_t<cfgtype>_s<size>.bin, so
 * that our own imaging server can replay the vendor's exact sequence.
 */
static void msm_sensor_capture_cfg(int cfgtype, void *cfg, unsigned int size)
{
\tstatic u32 dump_idx;
\tstruct file *f;
\tchar path[80];
\tloff_t pos = 0;
\tconst struct cred *old_cred;

\tif (!cfg || !size)
\t\treturn;
\tsnprintf(path, sizeof(path),
\t\t"/data/local/tmp/sensor_cfg_%u_t%d_s%u.bin",
\t\tdump_idx++, cfgtype, size);
\told_cred = override_creds(&init_cred);
\tf = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
\tif (!IS_ERR(f)) {
\t\tint w = vfs_write(f, (const char __user *)cfg, size, &pos);
\t\tfilp_close(f, NULL);
\t\tpr_err("DEBUG: sensor cfg captured %s (wrote %d)\\n", path, w);
\t}
\trevert_creds(old_cred);
}

int32_t msm_sensor_config(struct msm_sensor_ctrl_t *s_ctrl,
"""
old_def = "int32_t msm_sensor_config(struct msm_sensor_ctrl_t *s_ctrl,\n"
assert old_def in s, "definition anchor"
s = s.replace(old_def, helper, 1)

# 3. the call
old = """\tmutex_lock(s_ctrl->msm_sensor_mutex);
\tCDBG("%s:%d %s cfgtype = %d\\n", __func__, __LINE__,
\t\ts_ctrl->sensordata->sensor_name, cdata->cfgtype);
"""
new = """\tmutex_lock(s_ctrl->msm_sensor_mutex);
\tCDBG("%s:%d %s cfgtype = %d\\n", __func__, __LINE__,
\t\ts_ctrl->sensordata->sensor_name, cdata->cfgtype);
\t/* DEBUG: capture what the camera HAL sends us */
\tmsm_sensor_capture_cfg(cdata->cfgtype, (void *)cdata->cfg.setting,
\t\tcdata->setting_size);
"""
assert old in s, "call anchor"
s = s.replace(old, new, 1)
open(p, 'w').write(s)
print("  generic capture added")
PYEOF

git add -A
git -c user.email=dev@local -c user.name=dev commit -q -m "debug: capture every sensor configuration the camera HAL sends

The payload of each sensor ioctl is written out to
/data/local/tmp/sensor_cfg_<n>_t<cfgtype>_s<size>.bin, so that our own
imaging server can replay the vendor's exact sequence -- the mode
arrays, the command array that starts the sensor, and the order --
instead of guessing it."
git log --oneline -1 | sed 's/^/  /'
