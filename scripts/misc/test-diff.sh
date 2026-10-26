#!/bin/bash
cd /home/user/Develop/src/android-air-gapped || exit 1
echo "=== boot the differential kernel with the ROM's Android ==="
timeout 30 adb reboot bootloader 2>&1 | tr -d '\r' || true
for i in $(seq 1 40); do
  if timeout 8 fastboot devices 2>/dev/null | grep -q fastboot; then
    echo "  in fastboot after ${i}s"; break
  fi
  sleep 1
done
timeout 240 fastboot boot standalone/rootfs/stock/boot-ourkern-diff-android.img 2>&1 | tail -2 | sed 's/^/  /'
timeout 240 adb wait-for-device 2>&1 | sed 's/^/  /'
timeout 30 adb shell 'cat /proc/version' 2>&1 | tr -d '\r' | head -1 | sed 's/^/  /'

echo
echo "=== launch the camera app ==="
timeout 60 adb shell 'am start -n com.google.android.GoogleCamera/com.android.camera.CameraLauncher' 2>&1 | tr -d '\r' | head -2 | sed 's/^/  /'
sleep 18

echo
echo "=== camera clients ==="
timeout 40 adb shell 'dumpsys media.camera | grep -A3 "Active Camera Clients"' 2>&1 | tr -d '\r' | head -5 | sed 's/^/  /'

echo
echo "=== the captures ==="
timeout 40 adb shell 'ls -la /data/local/tmp/isp_cfg_*.bin 2>/dev/null' 2>&1 | tr -d '\r' | sed 's/^/  /'

echo
echo "=== the HAL's errors ==="
timeout 120 adb shell 'logcat -d 2>/dev/null | grep -iE "CameraDevice 0 error|CAM_FatalError|failed to open|camera open|openCamera" ' 2>&1 | tr -d '\r' | tail -12 | sed 's/^/  l: /'

echo
echo "=== pull the captures ==="
mkdir -p /home/user/Develop/src/android-air-gapped/standalone/cam/data
for f in $(timeout 30 adb shell 'ls /data/local/tmp/isp_cfg_*.bin 2>/dev/null' | tr -d '\r'); do
  b=$(basename "$f")
  timeout 120 adb pull "$f" "/home/user/Develop/src/android-air-gapped/standalone/cam/data/$b" 2>&1 | tail -1 | sed 's/^/  /'
done
ls -la /home/user/Develop/src/android-air-gapped/standalone/cam/data/ | sed 's/^/  /'
