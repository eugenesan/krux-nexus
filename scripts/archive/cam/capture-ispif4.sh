#!/bin/bash
# Dump the ISP-IF payload as raw bytes, read one byte at a time from the
# user's buffer (the direct reads work; copy_from_user of the whole
# struct does not, which is why the file is empty).
set -e
R=/home/user/Develop/src/android-air-gapped
T=$R/standalone/vendor/android_kernel_lge_hammerhead
cd $T
git checkout -q airgap/debug-capture

python3 - <<'PYEOF'
p = 'drivers/media/platform/msm/camera_v2/ispif/msm_ispif.c'
s = open(p).read()
start = s.index("\t\t\tif (d->cfg_type == ISPIF_CFG ||")
end = s.index("\t\t\tsnprintf(path, sizeof(path),", start)
new = """\t\t\tif (d->cfg_type == ISPIF_CFG ||
\t\t\t\td->cfg_type == ISPIF_START_FRAME_BOUNDARY) {
\t\t\t\tunsigned char *u = (unsigned char *)arg;
\t\t\t\tint k;

\t\t\t\tpr_err("DEBUG: ISP-IF raw payload, cfg type %d\\n",
\t\t\t\t\td->cfg_type);
\t\t\t\tfor (k = 0; k < 64; k += 16) {
\t\t\t\t\tpr_err("DEBUG:   %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\\n",
\t\t\t\t\t\tu[k + 0], u[k + 1], u[k + 2], u[k + 3],
\t\t\t\t\t\tu[k + 4], u[k + 5], u[k + 6], u[k + 7],
\t\t\t\t\t\tu[k + 8], u[k + 9], u[k + 10], u[k + 11],
\t\t\t\t\t\tu[k + 12], u[k + 13], u[k + 14], u[k + 15]);
\t\t\t\t}
\t\t\t}
"""
s = s[:start] + new + s[end:]
open(p, 'w').write(s)
print("  the payload is dumped byte by byte")
PYEOF

git add -A
git -c user.email=dev@local -c user.name=dev commit -q -m "debug: dump the ISP-IF payload byte by byte

copy_from_user of the whole struct fails (EFAULT) although the direct
reads work, so the payload is read one byte at a time and printed."
git log --oneline -1 | sed 's/^/  /'
