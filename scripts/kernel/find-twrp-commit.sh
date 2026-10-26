#!/bin/bash
# Look for the TWRP kernel's commit (da91d066bc6) in acroreiser's branches.
cd /home/user/Develop/src/android-air-gapped/standalone/vendor/android_kernel_lge_hammerhead.oct || exit 1
df -h /tmp | tail -1

echo "=== fetching the -kernelsu branches (blobless, so it is cheap) ==="
for b in lineage-22.2-kernelsu lineage-23.0-kernelsu lineage-23.2-kernelsu fbe-dirty-tree; do
  echo "--- $b"
  timeout 600 git fetch --filter=blob:none --no-tags origin "refs/heads/$b:refs/remotes/origin/$b" 2>&1 | tail -2
done

echo
echo "=== searching every fetched ref for da91d066bc6 ==="
git cat-file -t da91d066bc6 2>&1 | head -2
git log --all --oneline --format='%h %ad %s' --date=short 2>/dev/null | head -5
echo "--- commits around 2026-05-16..2026-05-20 on any branch:"
git log --all --since=2026-05-15 --until=2026-05-20 --format='%h %ad %d %s' --date=short 2>/dev/null | head -20
