#!/bin/bash
# The rear camera with the CPP firmware loaded.
cd /home/user/Develop/src/android-air-gapped || exit 1
timeout 60 adb push /tmp/opencode/imaging8 /data/local/tmp/imaging8 >/dev/null 2>&1
timeout 30 adb shell 'chmod 755 /data/local/tmp/imaging8' >/dev/null 2>&1
timeout 30 adb shell 'dmesg -c >/dev/null 2>&1' >/dev/null 2>&1
timeout 60 adb shell '/data/local/tmp/imaging8 640 480 0x40000000 rear 0 /data/local/tmp/cpp_firmware_v1_2_0.fw' 2>&1 | tr -d '\r' | head -30
echo
echo "=== kernel log ==="
timeout 60 adb shell 'dmesg | grep -viE "avc:|audit\(|selinux|charger|bq24192|max17048|regulator"' 2>&1 | tr -d '\r' | head -25 | sed 's/^/  k: /'
echo
echo "=== the frame file? ==="
timeout 30 adb shell 'ls -la /data/local/tmp/frame.nv12 2>/dev/null' 2>&1 | tr -d '\r' | sed 's/^/  /'
