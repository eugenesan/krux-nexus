#!/bin/bash
# Reference run: the ROM's own camera (which works), and its kernel log.
cd /home/user/Develop/src/android-air-gapped || exit 1

echo "=== reboot into the ROM ==="
timeout 30 adb reboot 2>&1 | tr -d '\r'
timeout 180 adb wait-for-device 2>&1 | sed 's/^/  /'
timeout 20 adb devices 2>&1 | sed 's/^/  /'
timeout 30 adb shell 'cat /proc/version' 2>&1 | tr -d '\r' | head -1 | sed 's/^/  /'

echo
echo "=== clear the log and open the camera ==="
timeout 30 adb shell 'su -c "dmesg -c"' >/dev/null 2>&1
timeout 40 adb shell 'su -c "am start -n com.google.android.GoogleCamera/com.google.android.apps.googlecamera.fishfood.MainActivity"' 2>&1 | tr -d '\r' | head -5 | sed 's/^/  /'
sleep 12

echo
echo "=== does the camera service see the devices? ==="
timeout 40 adb shell 'su -c "dumpsys media.camera | head -20"' 2>&1 | tr -d '\r' | head -20 | sed 's/^/  /'

echo
echo "=== the kernel's camera log of the reference run ==="
timeout 60 adb shell 'su -c "dmesg | grep -iE \"csid|csiphy|isp|camera|sensor|imx179|msm_cam|vcp|cpp\""' 2>&1 | tr -d '\r' | head -60 | sed 's/^/  k: /'
