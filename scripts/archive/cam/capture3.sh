#!/bin/bash
cd /home/user/Develop/src/android-air-gapped || exit 1
echo "=== the kernel's messages, via logcat (klogd) ==="
timeout 120 adb shell 'logcat -d -b all 2>/dev/null | grep -iE "chromatix|cam_config|reg_cfg|num_cfg|cmd_len|msm_isp|isp_|cpp|csiphy|csid|vcp" ' 2>&1 | tr -d '\r' | head -60 | sed 's/^/  l: /'
echo
echo "=== how much is there at all? ==="
timeout 120 adb shell 'logcat -d -b all 2>/dev/null | wc -l' 2>&1 | tr -d '\r' | sed 's/^/  lines: /'
timeout 120 adb shell 'logcat -d -b all 2>/dev/null | grep -c -i kernel' 2>&1 | tr -d '\r' | sed 's/^/  kernel-tagged lines: /'
