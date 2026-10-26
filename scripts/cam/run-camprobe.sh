#!/bin/bash
# Run the camera probe on our own kernel (root adb in TWRP).
cd /home/user/Develop/src/android-air-gapped || exit 1

for b in camprobe dqsize dump imaging2; do
  timeout 60 adb push /tmp/opencode/$b /data/local/tmp/$b >/dev/null 2>&1
done
timeout 30 adb shell 'chmod 755 /data/local/tmp/camprobe /data/local/tmp/dqsize /data/local/tmp/dump /data/local/tmp/imaging2' >/dev/null 2>&1

echo "=== camera nodes and their formats ==="
timeout 60 adb shell '/data/local/tmp/camprobe' 2>&1 | tr -d '\r' | head -80
