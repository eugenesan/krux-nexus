#!/bin/bash
cd /home/user/Develop/src/android-air-gapped || exit 1

echo "=== the kernel lines we have so far ==="
timeout 120 adb shell 'logcat -d -b all 2>/dev/null | grep -i "kernel"' 2>&1 | tr -d '\r' | head -12 | sed 's/^/  l: /'

echo
echo "=== start the camera app (the HAL will open the camera now) ==="
timeout 60 adb shell 'am start -n com.google.android.GoogleCamera/com.google.android.apps.googlecamera.fishfood.MainActivity' 2>&1 | tr -d '\r' | head -5 | sed 's/^/  /'
sleep 12

echo
echo "=== is a camera client active now? ==="
timeout 40 adb shell 'dumpsys media.camera | grep -A3 "Active Camera Clients"' 2>&1 | tr -d '\r' | head -6 | sed 's/^/  /'

echo
echo "=== the kernel lines now ==="
timeout 120 adb shell 'logcat -d -b all 2>/dev/null | grep -iE "chromatix|cam_config|reg_cfg|num_cfg|msm_isp|isp_|cpp|csiphy|csid|vcp|msm_camera"' 2>&1 | tr -d '\r' | head -50 | sed 's/^/  l: /'
