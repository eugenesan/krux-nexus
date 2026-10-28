#!/bin/bash
# Print the ISP-IF routing from inside msm_ispif_config(), where the
# parameters have already been read successfully (the kernel's own
# reads of them work; ours from the ioctl entry did not).
set -e
R=/home/user/Develop/src/android-air-gapped
T=$R/standalone/vendor/android_kernel_lge_hammerhead
cd $T
git checkout -q airgap/debug-capture

python3 - <<'PYEOF'
p = 'drivers/media/platform/msm/camera_v2/ispif/msm_ispif.c'
s = open(p).read()

# remove the ioctl-level capture (its reads from the user buffer did
# not work; the file it wrote was empty)
start = s.index("\tcase VIDIOC_MSM_ISPIF_CFG:\n\t\t/* DEBUG: capture what the camera HAL sends us.")
end = s.index("\t\treturn msm_ispif_cmd(sd, arg);", start) + len("\t\treturn msm_ispif_cmd(sd, arg);\n")
s = s[:start] + "\tcase VIDIOC_MSM_ISPIF_CFG:\n\t\treturn msm_ispif_cmd(sd, arg);\n" + s[end:]

# print the routing where the kernel reads it
old = """\tfor (i = 0; i < params->num; i++) {
\t\tvfe_intf = params->entries[i].vfe_intf;
"""
new = """\t/* DEBUG: what routing did the camera HAL ask for? */
\tfor (i = 0; i < params->num && i < 4; i++) {
\t\tpr_err("DEBUG: ISP-IF routing %d: vfe_intf %d intftype %d num_cids %d cid0 %d cid1 %d csid %d crop %d (%d..%d)\\n",
\t\t\ti,
\t\t\tparams->entries[i].vfe_intf,
\t\t\tparams->entries[i].intftype,
\t\t\tparams->entries[i].num_cids,
\t\t\tparams->entries[i].cids[0],
\t\t\tparams->entries[i].cids[1],
\t\t\tparams->entries[i].csid,
\t\t\tparams->entries[i].crop_enable,
\t\t\tparams->entries[i].crop_start_pixel,
\t\t\tparams->entries[i].crop_end_pixel);
\t}

""" + old
assert old in s, "config anchor"
s = s.replace(old, new, 1)
open(p, 'w').write(s)
print("  the routing is printed from msm_ispif_config()")
PYEOF

git add -A
git -c user.email=dev@local -c user.name=dev commit -q -m "debug: print the ISP-IF routing from msm_ispif_config()

The kernel's own reads of the parameters work (the ROM's camera uses
them); the reads from our ioctl-level capture did not, so the print
moved to where the driver reads them."
git log --oneline -1 | sed 's/^/  /'
