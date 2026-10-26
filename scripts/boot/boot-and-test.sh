#!/bin/bash
# Boot the ffs-fixed kernel with the pristine TWRP ramdisk and check adb.
cd /home/user/Develop/src/android-air-gapped || exit 1

echo "=== reboot to bootloader ==="
timeout 30 adb reboot bootloader 2>&1 | tr -d '\r'
for i in $(seq 1 40); do
  if timeout 8 fastboot devices 2>/dev/null | grep -q fastboot; then
    echo "  in fastboot after ${i}s"
    break
  fi
  sleep 1
done
timeout 10 fastboot devices 2>&1 | sed 's/^/  /'

echo "=== fastboot boot our fixed kernel + pristine TWRP ramdisk ==="
timeout 240 fastboot boot standalone/rootfs/stock/boot-ourkern-ffsfix-twrp.img 2>&1 | sed 's/^/  /'

echo "=== waiting for adb (up to 150s) ==="
timeout 150 adb wait-for-device 2>&1 | sed 's/^/  /'
timeout 20 adb devices -l 2>&1 | sed 's/^/  /'

echo "=== identity ==="
timeout 30 adb shell 'id; cat /proc/version' 2>&1 | tr -d '\r' | sed 's/^/  /'
