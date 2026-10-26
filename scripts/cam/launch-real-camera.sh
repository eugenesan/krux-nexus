#!/bin/bash
cd /home/user/Develop/src/android-air-gapped || exit 1
echo "=== the correct launcher activity ==="
timeout 40 adb shell 'cmd package resolve-activity --brief -a android.intent.action.MAIN -c android.intent.category.LAUNCHER com.google.android.GoogleCamera' 2>&1 | tr -d '\r' | tail -2 | sed 's/^/  /'

echo
echo "=== launch it ==="
timeout 60 adb shell 'am start -n com.google.android.GoogleCamera/com.android.camera.CameraLauncher' 2>&1 | tr -d '\r' | head -3 | sed 's/^/  /'
sleep 15

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
