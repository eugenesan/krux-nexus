#!/bin/bash
cd /home/user/Develop/src/android-air-gapped || exit 1
timeout 30 adb shell 'dmesg -c >/dev/null 2>&1' >/dev/null 2>&1
timeout 50 adb shell '/data/local/tmp/imaging7 640 480 0x40000000' 2>&1 | tr -d '\r' | sed 's/^/  /'
echo
echo "=== the whole kernel log ==="
timeout 60 adb shell 'dmesg | grep -viE "avc:|audit\(|selinux|charger|bq24192|max17048|regulator"' 2>&1 | tr -d '\r' | head -45 | sed 's/^/  k: /'
