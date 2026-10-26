#!/bin/bash
# Test the front camera (MT9M114B): the kernel driver programs the sensor,
# so nothing vendor-specific is involved.  If frames arrive, the pipeline
# is complete and only the IMX179's programming is in question.
cd /home/user/Develop/src/android-air-gapped || exit 1
timeout 60 adb push /tmp/opencode/imaging8 /data/local/tmp/imaging8 >/dev/null 2>&1
timeout 30 adb shell 'chmod 755 /data/local/tmp/imaging8' >/dev/null 2>&1
timeout 30 adb shell 'dmesg -c >/dev/null 2>&1' >/dev/null 2>&1
timeout 60 adb shell '/data/local/tmp/imaging8 640 480 0x40000000 front' 2>&1 | tr -d '\r' | head -30
echo
echo "=== kernel log ==="
timeout 60 adb shell 'dmesg | grep -viE "avc:|audit\(|selinux|charger|bq24192|max17048|regulator"' 2>&1 | tr -d '\r' | head -30 | sed 's/^/  k: /'
