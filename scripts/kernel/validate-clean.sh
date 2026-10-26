#!/bin/bash
# Boot the consolidated-tree kernel and validate everything.
R=/home/user/Develop/src/android-air-gapped
cd $R || exit 1

echo "=== reboot to bootloader ==="
timeout 30 adb reboot bootloader 2>&1 | tr -d '\r'
for i in $(seq 1 40); do
  if timeout 8 fastboot devices 2>/dev/null | grep -q fastboot; then
    echo "  in fastboot after ${i}s"; break
  fi
  sleep 1
done

echo
echo "=== boot ==="
timeout 240 fastboot boot standalone/rootfs/stock/boot-ourkern-clean-twrp.img 2>&1 | sed 's/^/  /'

echo
echo "=== wait for adb ==="
timeout 150 adb wait-for-device 2>&1 | sed 's/^/  /'
timeout 20 adb devices -l 2>&1 | sed 's/^/  /'

echo
echo "=== identity ==="
timeout 30 adb shell 'id' 2>&1 | tr -d '\r' | sed 's/^/  /'
timeout 30 adb shell 'cat /proc/version' 2>&1 | tr -d '\r' | sed 's/^/  /'

echo
echo "=== camera + usb sanity ==="
timeout 30 adb shell 'ls /dev/video* | tr "\n" " "' 2>&1 | tr -d '\r' | sed 's/^/  /'
timeout 40 adb shell 'dmesg | grep -c "unaligned request"' 2>&1 | tr -d '\r' | sed 's/^/  unaligned warnings: /'
timeout 30 adb shell 'dmesg | grep -c "DIAG"' 2>&1 | tr -d '\r' | sed 's/^/  leftover DIAG traces: /'

echo
echo "=== the imaging server handshake on this kernel ==="
timeout 60 adb push /tmp/opencode/imaging6 /data/local/tmp/imaging6 >/dev/null 2>&1
timeout 30 adb shell 'chmod 755 /data/local/tmp/imaging6' >/dev/null 2>&1
timeout 60 adb shell '/data/local/tmp/imaging6 10 /dev/video1' 2>&1 | tr -d '\r' | head -12
