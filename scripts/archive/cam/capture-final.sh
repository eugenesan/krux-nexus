#!/bin/bash
cd /home/user/Develop/src/android-air-gapped || exit 1
echo "=== which kernel are we on? ==="
timeout 30 adb shell 'cat /proc/version' 2>&1 | tr -d '\r' | head -1 | sed 's/^/  /'
timeout 30 adb shell 'zcat /proc/config.gz 2>/dev/null | grep -E "^CONFIG_(WLAN|BT|MSMB_CAMERA)="' 2>&1 | tr -d '\r' | sed 's/^/  /'

echo
echo "=== launch the camera ==="
timeout 60 adb shell 'am start -n com.google.android.GoogleCamera/com.android.camera.CameraLauncher' 2>&1 | tr -d '\r' | head -3 | sed 's/^/  /'
sleep 20

echo
echo "=== camera clients ==="
timeout 40 adb shell 'dumpsys media.camera | grep -A3 "Active Camera Clients"' 2>&1 | tr -d '\r' | head -5 | sed 's/^/  /'

echo
echo "=== the captures ==="
timeout 40 adb shell 'ls -la /data/local/tmp/isp_cfg_*.bin 2>/dev/null' 2>&1 | tr -d '\r' | sed 's/^/  /'

echo
echo "=== pull them ==="
mkdir -p /home/user/Develop/src/android-air-gapped/standalone/cam/data
for f in $(timeout 30 adb shell 'ls /data/local/tmp/isp_cfg_*.bin 2>/dev/null' | tr -d '\r'); do
  b=$(basename "$f")
  timeout 120 adb pull "$f" "/home/user/Develop/src/android-air-gapped/standalone/cam/data/$b" 2>&1 | tail -1 | sed 's/^/  /'
done
ls -la /home/user/Develop/src/android-air-gapped/standalone/cam/data/ | sed 's/^/  /'
