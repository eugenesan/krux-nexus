#!/bin/bash
# Find the IMX179 register tables in the ROM's vendor camera libraries.
cd /home/user/Develop/src/android-air-gapped || exit 1

echo "=== reboot into the ROM ==="
timeout 30 adb reboot 2>&1 | tr -d '\r'
timeout 180 adb wait-for-device 2>&1 | sed 's/^/  /'
timeout 20 adb devices 2>&1 | sed 's/^/  /'

echo
echo "=== which libraries mention imx179? ==="
timeout 120 adb shell 'grep -rl "imx179" /vendor/lib /system/lib /system/bin /vendor/bin 2>/dev/null | head -20' 2>&1 | tr -d '\r' | sed 's/^/  /'

echo
echo "=== camera-ish libraries in /vendor ==="
timeout 60 adb shell 'ls /vendor/lib/ 2>/dev/null | grep -iE "cam|sensor|isp" | head -20' 2>&1 | tr -d '\r' | sed 's/^/  /'
timeout 60 adb shell 'ls /system/lib/ 2>/dev/null | grep -iE "cam|sensor" | head -20' 2>&1 | tr -d '\r' | sed 's/^/  /'
