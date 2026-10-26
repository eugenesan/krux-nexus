#!/bin/bash
# Boot our debug kernel with the ROM's Android userspace: the ROM's camera
# HAL will open the camera and upload its chromatrix to *our* kernel, which
# logs it.
set -e
R=/home/user/Develop/src/android-air-gapped
cd $R
W=/tmp/opencode/kcmp

echo "=== pack: our debug kernel + the ROM's Android ramdisk ==="
rm -rf $W/romandroid && mkdir -p $W/romandroid
python3 standalone/rootfs/bootimg.py unpack standalone/rootfs/stock/boot.img \
    $W/romandroid | sed 's/^/  /'
cp /tmp/opencode/ko-dbg/arch/arm/boot/zImage-dtb $W/romandroid/kernel
python3 standalone/rootfs/bootimg.py pack $W/romandroid $W/romandroid/ramdisk \
    standalone/rootfs/stock/boot-ourkern-dbg-android.img | sed 's/^/  /'

echo
echo "=== reboot to bootloader and boot it ==="
timeout 30 adb reboot bootloader 2>&1 | tr -d '\r' || true
for i in $(seq 1 40); do
  if timeout 8 fastboot devices 2>/dev/null | grep -q fastboot; then
    echo "  in fastboot after ${i}s"; break
  fi
  sleep 1
done
timeout 240 fastboot boot standalone/rootfs/stock/boot-ourkern-dbg-android.img 2>&1 | tail -2 | sed 's/^/  /'
echo
echo "=== wait for adb (Android may take a while) ==="
timeout 240 adb wait-for-device 2>&1 | sed 's/^/  /'
timeout 20 adb devices 2>&1 | sed 's/^/  /'
timeout 30 adb shell 'cat /proc/version' 2>&1 | tr -d '\r' | head -1 | sed 's/^/  /'
