#!/bin/bash
# The differential build, as close to the ROM's kernel as we can get:
#   - no air-gap profile
#   - the ROM's compiler (gcc 16.2.0, the Arch toolchain)
set -e
R=/home/user/Develop/src/android-air-gapped
T=$R/standalone/vendor/android_kernel_lge_hammerhead
W=/tmp/opencode/kcmp
cd $T

echo "=== the toolchains we have ==="
ls -d $R/standalone/vendor/toolchain* 2>/dev/null | sed 's/^/  /'
ls $R/standalone/vendor/toolchain/ 2>/dev/null | head -6 | sed 's/^/    /'

echo
echo "=== build with the ROM's toolchain ==="
DEFCONFIG_NAME=twrp_hammerhead_defconfig TOOLCHAIN=arch PROFILE=/tmp/opencode/nostrip.list \
  bash $R/build/build-kernel.sh --tree $T --out /tmp/opencode/ko-diff2 \
  > /tmp/opencode/kodiff2.log 2>&1
grep -E '^\s+(ok|!!)' /tmp/opencode/kodiff2.log | tail -3 | sed 's/^/  /'
grep -E 'version: arm-none-eabi-gcc' /tmp/opencode/kodiff2.log | sed 's/^/  /'

echo
echo "=== pack ==="
cp /tmp/opencode/ko-diff2/arch/arm/boot/zImage-dtb $W/romandroid/kernel
python3 $R/standalone/rootfs/bootimg.py pack $W/romandroid $W/romandroid/ramdisk \
    $R/standalone/rootfs/stock/boot-ourkern-diff2-android.img | sed 's/^/  /'

echo
echo "=== boot it (and this time watch for it to come up) ==="
timeout 30 adb reboot bootloader 2>&1 | tr -d '\r' || true
for i in $(seq 1 40); do
  if timeout 8 fastboot devices 2>/dev/null | grep -q fastboot; then
    echo "  in fastboot after ${i}s"; break
  fi
  sleep 1
done
timeout 240 fastboot boot standalone/rootfs/stock/boot-ourkern-diff2-android.img 2>&1 | tail -2 | sed 's/^/  /'
sleep 25
timeout 30 adb shell 'cat /proc/version' 2>&1 | tr -d '\r' | head -1 | sed 's/^/  /'
timeout 20 adb devices 2>&1 | sed 's/^/  /'
