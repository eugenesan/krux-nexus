#!/bin/bash
# Capture what the camera HAL sends to the ISP-IF (VIDIOC_MSM_ISPIF_CFG):
# the routing between the CSID, the virtual channel and the VFE.
# This is the piece our imaging server never sent, and it is why the
# CSID's packets went nowhere.
set -e
R=/home/user/Develop/src/android-air-gapped
T=$R/standalone/vendor/android_kernel_lge_hammerhead
cd $T
git checkout -q airgap/debug-capture

python3 - <<'PYEOF'
p = 'drivers/media/platform/msm/camera_v2/ispif/msm_ispif.c'
s = open(p).read()
old = """\tswitch (cmd) {
\tcase VIDIOC_MSM_ISPIF_CFG:
\t\treturn msm_ispif_cmd(sd, arg);
"""
new = """\tswitch (cmd) {
\tcase VIDIOC_MSM_ISPIF_CFG:
\t\t/* DEBUG: capture what the camera HAL sends us.  The
\t\t * buffer is the user's, so vfs_write() can copy it. */
\t\t{
\t\t\tstatic u32 dump_idx;
\t\t\tstruct file *f;
\t\t\tchar path[80];
\t\t\tloff_t pos = 0;
\t\t\tconst struct cred *old_cred;
\t\t\tstruct ispif_cfg_data *d = (struct ispif_cfg_data *)arg;

\t\t\tsnprintf(path, sizeof(path),
\t\t\t\t"/data/local/tmp/ispif_cfg_%u_t%d.bin",
\t\t\t\tdump_idx++, d->cfg_type);
\t\t\told_cred = override_creds(&init_cred);
\t\t\tf = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
\t\t\tif (!IS_ERR(f)) {
\t\t\t\tint w = vfs_write(f, (const char __user *)arg,
\t\t\t\t\tsizeof(struct ispif_cfg_data), &pos);
\t\t\t\tfilp_close(f, NULL);
\t\t\t\tpr_err("DEBUG: ISP-IF config captured %s (wrote %d)\\n",
\t\t\t\t\tpath, w);
\t\t\t}
\t\t\trevert_creds(old_cred);
\t\t}
\t\treturn msm_ispif_cmd(sd, arg);
"""
assert old in s, "anchor"
s = s.replace(old, new, 1)
if '#include <linux/cred.h>' not in s:
    s = s.replace('#include <linux/module.h>',
                  '#include <linux/module.h>\n#include <linux/fs.h>\n#include <linux/cred.h>\n#include <linux/init_task.h>', 1)
open(p, 'w').write(s)
print("  the ISP-IF capture is in")
PYEOF

grep -n '#include <linux/module.h>\|#include <linux/fs.h>' drivers/media/platform/msm/camera_v2/ispif/msm_ispif.c | head -3 | sed 's/^/  /'
git add -A
git -c user.email=dev@local -c user.name=dev commit -q -m "debug: capture the ISP-IF configuration the camera HAL sends

VIDIOC_MSM_ISPIF_CFG is how the camera2 stack tells the ISP-IF which
CSID, which virtual channel, which VFE interface and which crop to
route the incoming pixels to -- and then starts it.  Our own imaging
server never sent any of it, which is why the CSID's packets (11,808
of them, no errors) went nowhere."
git log --oneline -1 | sed 's/^/  /'
