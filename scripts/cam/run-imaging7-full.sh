#!/bin/bash
# Run the imaging server and capture the WHOLE kernel log of the run.
cd /home/user/Develop/src/android-air-gapped || exit 1
timeout 30 adb shell 'dmesg -c >/dev/null 2>&1' >/dev/null 2>&1
timeout 45 adb shell '/data/local/tmp/imaging7 640 480' 2>&1 | tr -d '\r' | head -12
echo
echo "=== the whole kernel log of the run (isp / csid / csiphy / sensor) ==="
timeout 60 adb shell 'dmesg | grep -viE "avc:|audit\(|selinux|charger|bq24192|max17048|regulator"' 2>&1 | tr -d '\r' | head -60
