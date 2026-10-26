#!/bin/bash
# Capture what the ROM's camera HAL uploads to our kernel.
cd /home/user/Develop/src/android-air-gapped || exit 1

echo "=== wait for the camera provider ==="
sleep 20
timeout 30 adb shell 'dumpsys media.camera | head -12' 2>&1 | tr -d '\r' | sed 's/^/  /'

echo
echo "=== the kernel log size ==="
timeout 60 adb shell 'dmesg | wc -l' 2>&1 | tr -d '\r' | sed 's/^/  /'

echo
echo "=== the camera-related lines ==="
timeout 120 adb shell 'dmesg | grep -iE "isp|chromatix|cam_config|reg_cfg|num_cfg|cmd_len|csiphy|csid|cpp|vcp"' 2>&1 | tr -d '\r' | head -60 | sed 's/^/  k: /'

echo
echo "=== save the whole log ==="
timeout 180 adb shell 'dmesg' > /tmp/opencode/rom-cam-dmesg.txt 2>/dev/null
wc -l /tmp/opencode/rom-cam-dmesg.txt
