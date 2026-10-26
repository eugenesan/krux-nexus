#!/bin/bash
# Rebuild from the consolidated tree (upstream tip + the two fixes) using
# upstream's own defconfig, and re-validate on the device.
set -e
R=/home/user/Develop/src/android-air-gapped
cd $R

echo "=== tree auto-detection (no --tree) ==="
TOOLCHAIN=armgnu bash build/build-kernel.sh --check 2>&1 | head -12

echo
echo "=== build (upstream's own lineageos_hammerhead_defconfig) ==="
TOOLCHAIN=armgnu bash build/build-kernel.sh --out /tmp/opencode/ko-clean \
  > /tmp/opencode/koclean.log 2>&1
grep -E '^\s+(ok|!!)' /tmp/opencode/koclean.log | tail -5 | sed 's/^/  /'
ls -la /tmp/opencode/ko-clean/arch/arm/boot/zImage-dtb

echo
echo "=== which defconfig was used? ==="
grep -m1 'baseline' /tmp/opencode/koclean.log | sed 's/^/  /'
grep -E '^CONFIG_(HZ|DEVTMPFS|USB_G_ANDROID|MSMB_CAMERA|IMX179)=|^# CONFIG_DEVTMPFS' \
     /tmp/opencode/ko-clean/.config | sed 's/^/  /'

echo
echo "=== pack with the pristine TWRP ramdisk ==="
W=/tmp/opencode/kcmp
rm -rf $W/clean && cp -a $W/twrpgenuine $W/clean
cp /tmp/opencode/ko-clean/arch/arm/boot/zImage-dtb $W/clean/kernel
python3 standalone/rootfs/bootimg.py pack $W/clean $W/clean/ramdisk \
  standalone/rootfs/stock/boot-ourkern-clean-twrp.img
