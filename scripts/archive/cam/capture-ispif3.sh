#!/bin/bash
# Print the ISP-IF routing in the simplest possible way (one printk per
# field), and write only the first 16 bytes of the payload, to find out
# why the copy fails.
set -e
R=/home/user/Develop/src/android-air-gapped
T=$R/standalone/vendor/android_kernel_lge_hammerhead
cd $T
git checkout -q airgap/debug-capture

python3 - <<'PYEOF'
p = 'drivers/media/platform/msm/camera_v2/ispif/msm_ispif.c'
s = open(p).read()
old = """\t\t\tif (d->cfg_type == ISPIF_CFG ||
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
"""
new = """\t\t\tif (d->cfg_type == ISPIF_CFG ||
\t\t\t\td->cfg_type == ISPIF_START_FRAME_BOUNDARY) {
\t\t\t\tint k;
\t\t\t\tpr_err("DEBUG: ISP-IF cfg type %d, %d entries\\n",
\t\t\t\t\td->cfg_type, d->params.num);
\t\t\t\tfor (k = 0; k < d->params.num && k < 4; k++) {
\t\t\t\t\tpr_err("DEBUG: ISP-IF entry %d\\n", k);
\t\t\t\t\tpr_err("DEBUG:   vfe_intf %d\\n",
\t\t\t\t\t\td->params.entries[k].vfe_intf);
\t\t\t\t\tpr_err("DEBUG:   intftype %d\\n",
\t\t\t\t\t\td->params.entries[k].intftype);
\t\t\t\t\tpr_err("DEBUG:   num_cids %d\\n",
\t\t\t\t\t\td->params.entries[k].num_cids);
\t\t\t\t\tpr_err("DEBUG:   cid0 %d\\n",
\t\t\t\t\t\td->params.entries[k].cids[0]);
\t\t\t\t\tpr_err("DEBUG:   cid1 %d\\n",
\t\t\t\t\t\td->params.entries[k].cids[1]);
\t\t\t\t\tpr_err("DEBUG:   csid %d\\n",
\t\t\t\t\t\td->params.entries[k].csid);
\t\t\t\t\tpr_err("DEBUG:   crop_enable %d\\n",
\t\t\t\t\t\td->params.entries[k].crop_enable);
\t\t\t\t\tpr_err("DEBUG:   crop_start %d\\n",
\t\t\t\t\t\td->params.entries[k].crop_start_pixel);
\t\t\t\t\tpr_err("DEBUG:   crop_end %d\\n",
\t\t\t\t\t\td->params.entries[k].crop_end_pixel);
\t\t\t\t}
\t\t\t}
"""
assert old in s, "print anchor"
s = s.replace(old, new, 1)
old = """\t\t\t\tint w = vfs_write(f, (const char __user *)arg,
\t\t\t\t\t_IOC_SIZE(cmd), &pos);
"""
new = """\t\t\t\tint w = vfs_write(f, (const char __user *)arg,
\t\t\t\t\t16, &pos);
"""
assert old in s, "write anchor"
s = s.replace(old, new, 1)
open(p, 'w').write(s)
print("  the routing is printed field by field; the write is 16 bytes")
PYEOF

git add -A
git -c user.email=dev@local -c user.name=dev commit -q -m "debug: print the ISP-IF routing field by field

The multi-argument printk did not show up; one printk per field is
simple enough to be certain.  The file write is reduced to 16 bytes to
find out whether the copy fails because of the size."
git log --oneline -1 | sed 's/^/  /'
