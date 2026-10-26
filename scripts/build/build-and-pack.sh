#!/bin/bash
# Rebuild the kernel and package it with the pristine TWRP ramdisk.
set -e
R=/home/user/Develop/src/android-air-gapped
cd $R

DEFCONFIG_NAME=twrp_hammerhead_defconfig TOOLCHAIN=armgnu \
  bash $R/build/build-kernel.sh --tree $R/standalone/vendor/android_kernel_lge_hammerhead \
  --out /tmp/opencode/ko-kernel > /tmp/opencode/kokernel.log 2>&1
grep -E '^\s+(ok|!!)' /tmp/opencode/kokernel.log | tail -4
ls -la /tmp/opencode/ko-kernel/arch/arm/boot/zImage-dtb

W=/tmp/opencode/kcmp
rm -rf $W/fix1 && cp -a $W/twrpgenuine $W/fix1
cp /tmp/opencode/ko-kernel/arch/arm/boot/zImage-dtb $W/fix1/kernel
python3 standalone/rootfs/bootimg.py pack $W/fix1 $W/fix1/ramdisk \
  standalone/rootfs/stock/boot-ourkern-ffsfix-twrp.img
ls -la standalone/rootfs/stock/boot-ourkern-ffsfix-twrp.img
echo "=== ccache ==="
ccache -s | head -5
