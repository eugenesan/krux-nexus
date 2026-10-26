#!/bin/bash
# Pack the rebased kernel with the pristine TWRP ramdisk, boot it, test adb.
R=/home/user/Develop/src/android-air-gapped
cd $R || exit 1
W=/tmp/opencode/kcmp

echo "=== pack ==="
rm -rf $W/rebase && cp -a $W/twrpgenuine $W/rebase
cp /tmp/opencode/ko-rebase/arch/arm/boot/zImage-dtb $W/rebase/kernel
python3 standalone/rootfs/bootimg.py pack $W/rebase $W/rebase/ramdisk \
  standalone/rootfs/stock/boot-ourkern-rebase-twrp.img

echo
echo "=== reboot to bootloader ==="
timeout 30 adb reboot bootloader 2>&1 | tr -d '\r'
for i in $(seq 1 40); do
  if timeout 8 fastboot devices 2>/dev/null | grep -q fastboot; then
    echo "  in fastboot after ${i}s"; break
  fi
  sleep 1
done

echo
echo "=== boot the rebased kernel ==="
timeout 240 fastboot boot standalone/rootfs/stock/boot-ourkern-rebase-twrp.img 2>&1 | sed 's/^/  /'

echo
echo "=== wait for adb ==="
timeout 150 adb wait-for-device 2>&1 | sed 's/^/  /'
timeout 20 adb devices -l 2>&1 | sed 's/^/  /'

echo
echo "=== identity ==="
timeout 30 adb shell 'id' 2>&1 | tr -d '\r' | sed 's/^/  /'
timeout 30 adb shell 'cat /proc/version' 2>&1 | tr -d '\r' | sed 's/^/  /'
echo
echo "=== camera nodes ==="
timeout 30 adb shell 'ls /dev/video* /dev/v4l-subdev* | tr "\n" " "' 2>&1 | tr -d '\r' | sed 's/^/  /'
echo
echo "=== the unaligned-request warning must be gone ==="
timeout 40 adb shell 'dmesg | grep -c "unaligned request"' 2>&1 | tr -d '\r' | sed 's/^/  count: /'
