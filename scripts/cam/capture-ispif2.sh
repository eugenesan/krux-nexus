#!/bin/bash
# The ISP-IF capture: the write failed with EFAULT because the camera2
# HAL's struct is smaller than this kernel's (the ioctl number carries
# the size the HAL used).  So: write exactly _IOC_SIZE(cmd) bytes from
# the user's buffer, and print the routing as well.
set -e
R=/home/user/Develop/src/android-air-gapped
T=$R/standalone/vendor/android_kernel_lge_hammerhead
cd $T
git checkout -q airgap/debug-capture

python3 - <<'PYEOF'
p = 'drivers/media/platform/msm/camera_v2/ispif/msm_ispif.c'
s = open(p).read()
old = """\t\t\tsnprintf(path, sizeof(path),
\t\t\t\t"/data/local/tmp/ispif_cfg_%u_t%d.bin",
\t\t\t\tdump_idx++, d->cfg_type);
"""
new = """\t\t\tif (d->cfg_type == ISPIF_CFG ||
\t\t\t\td->cfg_type == ISPIF_START_FRAME_BOUNDARY) {
\t\t\t\tint k;
\t\t\t\tpr_err("DEBUG: ISP-IF routing: %d entries\\n",
\t\t\t\t\td->params.num);
\t\t\t\tfor (k = 0; k < d->params.num && k < 4; k++)
\t\t\t\t\tpr_err("DEBUG:   entry %d: vfe_intf %d intftype %d num_cids %d cid0 %d csid %d crop %d (%d..%d)\\n",
\t\t\t\t\t\tk, d->params.entries[k].vfe_intf,
\t\t\t\t\t\td->params.entries[k].intftype,
\t\t\t\t\t\td->params.entries[k].num_cids,
\t\t\t\t\t\td->params.entries[k].cids[0],
\t\t\t\t\t\td->params.entries[k].csid,
\t\t\t\t\t\td->params.entries[k].crop_enable,
\t\t\t\t\t\td->params.entries[k].crop_start_pixel,
\t\t\t\t\t\td->params.entries[k].crop_end_pixel);
\t\t\t}
\t\t\tsnprintf(path, sizeof(path),
\t\t\t\t"/data/local/tmp/ispif_cfg_%u_t%d_s%u.bin",
\t\t\t\tdump_idx++, d->cfg_type, _IOC_SIZE(cmd));
"""
assert old in s, "name anchor"
s = s.replace(old, new, 1)

old = """\t\t\t\tint w = vfs_write(f, (const char __user *)arg,
\t\t\t\t\tsizeof(struct ispif_cfg_data), &pos);
"""
new = """\t\t\t\tint w = vfs_write(f, (const char __user *)arg,
\t\t\t\t\t_IOC_SIZE(cmd), &pos);
"""
assert old in s, "write anchor"
s = s.replace(old, new, 1)
open(p, 'w').write(s)
print("  the capture now uses the ioctl's own size and prints the routing")
PYEOF

git add -A
git -c user.email=dev@local -c user.name=dev commit -q -m "debug: write the ISP-IF capture with the ioctl's own size, and print the routing

The camera2 HAL's struct is smaller than this kernel's, so copying
sizeof(struct ispif_cfg_data) from the user's buffer ran past the end
of it (EFAULT).  The ioctl number carries the size the HAL used."
git log --oneline -1 | sed 's/^/  /'
