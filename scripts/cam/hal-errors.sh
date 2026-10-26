#!/bin/bash
cd /home/user/Develop/src/android-air-gapped || exit 1
echo "=== the HAL's own log around the camera open ==="
timeout 120 adb shell 'logcat -d 2>/dev/null | grep -iE "camx|qcam|mm-qcam|cameraserver|hal_3|ISP|camera provider|openCamera|Failed to|no camera" ' 2>&1 | tr -d '\r' | tail -35 | sed 's/^/  l: /'
