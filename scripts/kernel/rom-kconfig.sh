#!/bin/bash
# Boot the ROM's own kernel (its /boot image) and read its config.
cd /home/user/Develop/src/android-air-gapped || exit 1
echo "=== boot the ROM's own image ==="
timeout 30 adb reboot bootloader 2>&1 | tr -d '\r' || true
for i in $(seq 1 40); do
  if timeout 8 fastboot devices 2>/dev/null | grep -q fastboot; then
    echo "  in fastboot after ${i}s"; break
  fi
  sleep 1
done
timeout 240 fastboot boot standalone/rootfs/stock/boot.img 2>&1 | tail -2 | sed 's/^/  /'
timeout 240 adb wait-for-device 2>&1 | sed 's/^/  /'
timeout 30 adb shell 'cat /proc/version' 2>&1 | tr -d '\r' | head -1 | sed 's/^/  /'

echo
echo "=== the ROM kernel's camera configuration ==="
timeout 60 adb shell 'zcat /proc/config.gz 2>/dev/null | grep -E "CAMERA|MSMB|IMX179|MT9M114|CPP|CSIPHY|CSID|ISPIF" | sort' 2>&1 | tr -d '\r' | sed 's/^/  /'
