#!/bin/bash
# The air-gap profile DOES boot the ROM's Android (the radio-enabled
# builds die).  Build it with the capture patches and the ROM's compiler,
# boot it with the ROM's ramdisk, open the camera, and collect the
# chromatrix.
set -e
R=/home/user/Develop/src/android-air-gapped
T=$R/standalone/vendor/android_kernel_lge_hammerhead
W=/tmp/opencode/kcmp
cd $T

echo "=== build (air-gap profile, the ROM's toolchain) ==="
DEFCONFIG_NAME=lineageos_hammerhead_defconfig TOOLCHAIN=arch \
  bash $R/build/build-kernel.sh --tree $T --out /tmp/opencode/ko-cap \
  > /tmp/opencode/kocap.log 2>&1
grep -E '^\s+(ok|!!)' /tmp/opencode/kocap.log | tail -4 | sed 's/^/  /'
grep -cE ' error' /tmp/opencode/ko-cap.build.log 2>/dev/null | sed 's/^/  errors: /' || true

echo
echo "=== pack with the ROM's Android ramdisk (permissive selinux for dmesg) ==="
mkdir -p $W/romandroid
cp /tmp/opencode/ko-cap/arch/arm/boot/zImage-dtb $W/romandroid/kernel
cp /tmp/opencode/romcheck/ramdisk $W/romandroid/ramdisk
cd $R
python3 standalone/rootfs/bootimg.py pack $W/romandroid $W/romandroid/ramdisk \
    standalone/rootfs/stock/boot-ourkern-cap-android.img | sed 's/^/  /'

echo
echo "=== boot it and watch for OUR kernel ==="
timeout 30 adb reboot bootloader 2>&1 | tr -d '\r' || true
for i in $(seq 1 40); do
  timeout 8 fastboot devices 2>/dev/null | grep -q fastboot && { echo "  fastboot after ${i}s"; break; }
  sleep 1
done
timeout 240 fastboot boot standalone/rootfs/stock/boot-ourkern-cap-android.img 2>&1 | tail -2 | sed 's/^/  /'
for i in $(seq 1 60); do
  st=$(timeout 6 adb get-state 2>/dev/null | tr -d '\r')
  if [ "$st" = "device" ]; then
    v=$(timeout 15 adb shell 'cat /proc/version' 2>/dev/null | tr -d '\r' | head -1)
    echo "  [${i}] $v"
    if echo "$v" | grep -qE 'g[0-9a-f]{10}'; then
      echo "  >>> our kernel is up"
      break
    fi
  fi
  sleep 2
done
