#!/bin/bash
# What does acroreiser have upstream right now?
for r in acroreiser/android_kernel_lge_hammerhead acroreiser/android_device_lge_hammerhead; do
  echo "=== $r ==="
  timeout 30 curl -sL -H 'Accept: application/vnd.github+json' \
    "https://api.github.com/repos/$r" 2>/dev/null | python3 -c '
import sys,json
d=json.load(sys.stdin)
print("  default_branch:", d.get("default_branch"))
print("  pushed_at:", d.get("pushed_at"))
print("  description:", (d.get("description") or "")[:90])
'
  echo "  -- recent commits on the default branch:"
  timeout 30 curl -sL -H 'Accept: application/vnd.github+json' \
    "https://api.github.com/repos/$r/commits?per_page=8" 2>/dev/null | python3 -c '
import sys,json
d=json.load(sys.stdin)
if isinstance(d,dict):
    print("   ", d.get("message"))
else:
    for c in d:
        print("   ", c["sha"][:12], c["commit"]["author"]["date"][:10], c["commit"]["message"].splitlines()[0][:70])
'
  echo
done
