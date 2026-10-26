#!/bin/bash
cd /home/user/Develop/src/android-air-gapped || exit 1

echo "=== /vendor/firmware, camera-ish ==="
timeout 60 adb shell 'su -c "ls /vendor/firmware | grep -iE \"cam|cpp|isp|proc|sensor\""' 2>&1 | tr -d '\r' | sed 's/^/  /'

echo
echo "=== the firmware name the vendor's cpp module passes to the kernel ==="
timeout 60 adb shell 'su -c "cp /vendor/lib/libmmcamera2_cpp_module.so /data/local/tmp/ && chmod 644 /data/local/tmp/libmmcamera2_cpp_module.so"' 2>&1 | tr -d '\r' | sed 's/^/  /'
timeout 120 adb pull /data/local/tmp/libmmcamera2_cpp_module.so /home/user/Develop/src/android-air-gapped/standalone/cam/data/vendor/rom-libs/ 2>&1 | tail -1 | sed 's/^/  /'
if [ -f /home/user/Develop/src/android-air-gapped/standalone/cam/data/vendor/rom-libs/libmmcamera2_cpp_module.so ]; then
  strings /home/user/Develop/src/android-air-gapped/standalone/cam/data/vendor/rom-libs/libmmcamera2_cpp_module.so | grep -iE '\.bin|firmware|cpp_|cpp\.' | head -20 | sed 's/^/  s: /'
fi
