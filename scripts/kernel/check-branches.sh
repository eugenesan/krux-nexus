#!/bin/bash
# Where does our May base (299b2e18) live upstream, and is there a TWRP tree?
cd /home/user/Develop/src/android-air-gapped/standalone/vendor/android_kernel_lge_hammerhead.oct || exit 1

echo "=== which upstream branches contain 299b2e18f65d? ==="
for b in $(git for-each-ref --format='%(refname:short)' refs/remotes/origin); do
  if git merge-base --is-ancestor 299b2e18f65d "$b" 2>/dev/null; then
    echo "  $b contains it"
  fi
done
git branch -r --contains 299b2e18f65d 2>/dev/null | sed 's/^/  /' | head

echo
echo "=== acroreiser's repositories ==="
timeout 40 curl -sL -H 'Accept: application/vnd.github+json' \
  "https://api.github.com/users/acroreiser/repos?per_page=100&sort=updated" 2>/dev/null | \
python3 -c '
import sys,json
d=json.load(sys.stdin)
for r in d:
    if any(k in r["name"].lower() for k in ("hammerhead","kernel","twrp","lge")):
        print("  %-45s %s  branch=%s" % (r["name"], r["pushed_at"][:10], r["default_branch"]))
'

echo
echo "=== does the device tree reference a kernel commit? ==="
timeout 40 curl -sL -H 'Accept: application/vnd.github+json' \
  "https://api.github.com/repos/acroreiser/android_device_lge_hammerhead/contents/" 2>/dev/null | \
python3 -c '
import sys,json
d=json.load(sys.stdin)
if isinstance(d,dict):
    print("  ", d.get("message"))
else:
    for e in d:
        print("  %-30s %s" % (e["name"], e["type"]))
'
