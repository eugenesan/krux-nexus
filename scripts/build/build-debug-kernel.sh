#!/bin/bash
# Build a kernel with the camera_v2 debug logging on, so that the ROM's
# camera HAL shows us exactly what it uploads to the ISP.
set -e
R=/home/user/Develop/src/android-air-gapped
cd $R

echo "=== turn the camera debug logging on (temporarily) ==="
cp standalone/tools/profiles/hammerhead-camera.list /tmp/opencode/hammerhead-camera.list.bak
sed -i 's/^CONFIG_MSMB_CAMERA_DEBUG=n$/CONFIG_MSMB_CAMERA_DEBUG=y/' \
    standalone/tools/profiles/hammerhead-camera.list
grep -n 'MSMB_CAMERA_DEBUG' standalone/tools/profiles/hammerhead-camera.list | sed 's/^/  /'

echo
echo "=== build ==="
DEFCONFIG_NAME=twrp_hammerhead_defconfig TOOLCHAIN=armgnu \
  bash build/build-kernel.sh --tree $R/standalone/vendor/android_kernel_lge_hammerhead \
  --out /tmp/opencode/ko-dbg > /tmp/opencode/kodbg.log 2>&1
grep -E '^\s+(ok|!!)' /tmp/opencode/kodbg.log | tail -3 | sed 's/^/  /'

echo
echo "=== restore the profile ==="
cp /tmp/opencode/hammerhead-camera.list.bak standalone/tools/profiles/hammerhead-camera.list
git -C $R status --short standalone/tools/profiles/hammerhead-camera.list | sed 's/^/  /'
grep -n 'MSMB_CAMERA_DEBUG' standalone/tools/profiles/hammerhead-camera.list | sed 's/^/  /'
grep -E '^CONFIG_MSMB_CAMERA_DEBUG=' /tmp/opencode/ko-dbg/.config | sed 's/^/  built: /'
ls -la /tmp/opencode/ko-dbg/arch/arm/boot/zImage-dtb | sed 's/^/  /'
