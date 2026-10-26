#!/bin/bash
cd /home/user/Develop/src/android-air-gapped || exit 1
echo "=== reboot to bootloader and boot the debug kernel + the ROM's Android ==="
timeout 30 adb reboot bootloader 2>&1 | tr -d '\r' || true
for i in $(seq 1 40); do
  if timeout 8 fastboot devices 2>/dev/null | grep -q fastboot; then
    echo "  in fastboot after ${i}s"; break
  fi
  sleep 1
done
timeout 240 fastboot boot standalone/rootfs/stock/boot-ourkern-dbg-android.img 2>&1 | tail -2 | sed 's/^/  /'
timeout 240 adb wait-for-device 2>&1 | sed 's/^/  /'
timeout 20 adb devices 2>&1 | sed 's/^/  /'

echo
echo "=== can the shell read the kernel log now? ==="
timeout 60 adb shell 'dmesg | wc -l' 2>&1 | tr -d '\r' | sed 's/^/  /'

echo
echo "=== the camera lines ==="
timeout 120 adb shell 'dmesg | grep -iE "chromatix|cam_config|reg_cfg|num_cfg|cmd_len|msm_isp|isp_|cpp|csiphy|csid|vcp"' 2>&1 | tr -d '\r' | head -40 | sed 's/^/  k: /'

echo
echo "=== save the whole log ==="
timeout 240 adb shell 'dmesg' > /tmp/opencode/rom-cam-dmesg.txt 2>/dev/null
wc -l /tmp/opencode/rom-cam-dmesg.txt
