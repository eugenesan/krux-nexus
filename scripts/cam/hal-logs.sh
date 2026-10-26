#!/bin/bash
cd /home/user/Develop/src/android-air-gapped || exit 1
echo "=== the camera HAL's complaints ==="
timeout 120 adb shell 'logcat -d 2>/dev/null | grep -iE "camera|camx|qcamera|mm-qcam|isp|hal_3" ' 2>&1 | tr -d '\r' | tail -40 | sed 's/^/  l: /'
echo
echo "=== errors only ==="
timeout 120 adb shell 'logcat -d *:E 2>/dev/null | tail -30' 2>&1 | tr -d '\r' | sed 's/^/  E: /'
