#!/bin/bash
# Differential build: the same tree, but WITHOUT the air-gap profile, so
# that the kernel is as close as possible to the ROM's.  If the vendor
# HAL can open a camera on this one, the profile was what broke it.
set -e
R=/home/user/Develop/src/android-air-gapped
T=$R/standalone/vendor/android_kernel_lge_hammerhead
W=/tmp/opencode/kcmp
cd $T

echo "=== use the branch with the capture patch ==="
git checkout -q airgap/debug-capture
git log --oneline -1 | sed 's/^/  /'

echo
echo "=== a profile that strips nothing ==="
printf '# the differential build: strip nothing (the ROM has no air-gap profile)\nCONFIG_MSM_CAMERA_V4L2=n\n' > /tmp/opencode/nostrip.list
cat /tmp/opencode/nostrip.list | sed 's/^/  /'

echo
echo "=== build ==="
DEFCONFIG_NAME=twrp_hammerhead_defconfig TOOLCHAIN=armgnu PROFILE=/tmp/opencode/nostrip.list \
  bash $R/build/build-kernel.sh --tree $T --out /tmp/opencode/ko-diff \
  > /tmp/opencode/kodiff.log 2>&1
grep -E '^\s+(ok|!!)' /tmp/opencode/kodiff.log | tail -4 | sed 's/^/  /'
echo "  --- camera options in the build:"
grep -E '^CONFIG_(MSMB_CAMERA|IMX179|MT9M114B|MSM_CPP|MSM_CSID|MSM_CSIPHY|MSM_ISPIF)=' /tmp/opencode/ko-diff/.config | sed 's/^/    /'
echo "  --- and the things the air-gap build strips (now kept):"
grep -E '^CONFIG_(WLAN|BCMDHD|CFG80211|MSM_PIL_MSS_QDSP6V5|BT)=' /tmp/opencode/ko-diff/.config | sed 's/^/    /'

echo
echo "=== pack with the ROM's Android ramdisk ==="
cp /tmp/opencode/ko-diff/arch/arm/boot/zImage-dtb $W/romandroid/kernel
python3 $R/standalone/rootfs/bootimg.py pack $W/romandroid $W/romandroid/ramdisk \
    $R/standalone/rootfs/stock/boot-ourkern-diff-android.img | sed 's/^/  /'
