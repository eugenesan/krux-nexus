#!/bin/bash
cd /home/user/Develop/src/android-air-gapped || exit 1
echo "=== the camera-related kernel lines (as root) ==="
timeout 120 adb shell 'su -c "dmesg | grep -iE \"isp|chromatix|cam_config|reg_cfg|num_cfg|cmd_len|csiphy|csid|cpp|vcp|msm_camera\""' 2>&1 | tr -d '\r' | head -70 | sed 's/^/  k: /'
echo
echo "=== save the whole log ==="
timeout 240 adb shell 'su -c "dmesg"' > /tmp/opencode/rom-cam-dmesg.txt 2>/dev/null
wc -l /tmp/opencode/rom-cam-dmesg.txt
