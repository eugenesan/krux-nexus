#!/bin/bash
cd /home/user/Develop/src/android-air-gapped || exit 1

echo "=== the kernel buffer in logcat ==="
timeout 120 adb shell 'logcat -b kernel -d 2>/dev/null | wc -l' 2>&1 | tr -d '\r' | sed 's/^/  lines: /'
timeout 120 adb shell 'logcat -b kernel -d 2>/dev/null | grep -icE "isp|chromatix|cpp|csiphy"' 2>&1 | tr -d '\r' | sed 's/^/  camera lines: /'

echo
echo "=== start the camera with the standard intent ==="
timeout 60 adb shell 'am start -a android.media.action.IMAGE_CAPTURE' 2>&1 | tr -d '\r' | head -4 | sed 's/^/  /'
sleep 15

echo
echo "=== camera clients ==="
timeout 40 adb shell 'dumpsys media.camera | grep -A2 "Active Camera Clients"' 2>&1 | tr -d '\r' | head -4 | sed 's/^/  /'

echo
echo "=== the kernel buffer now ==="
timeout 120 adb shell 'logcat -b kernel -d 2>/dev/null | wc -l' 2>&1 | tr -d '\r' | sed 's/^/  lines: /'
timeout 120 adb shell 'logcat -b kernel -d 2>/dev/null | grep -iE "isp|chromatix|cpp|csiphy|csid|vcp|reg_cfg" | head -40' 2>&1 | tr -d '\r' | sed 's/^/  k: /'
