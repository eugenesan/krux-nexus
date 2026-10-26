#!/bin/bash
# Build our kernel with the ROM's EXACT configuration (read out of the
# running kernel's /proc/config.gz), with the ROM's compiler, and with the
# capture patch.  This is the closest we can get to the ROM's own kernel.
set -e
R=/home/user/Develop/src/android-air-gapped
T=$R/standalone/vendor/android_kernel_lge_hammerhead
TC=$R/standalone/vendor/toolchain/usr/bin
OUT=/tmp/opencode/ko-romcfg
export PATH="$TC:$PATH"

cd $T
git checkout -q airgap/debug-capture
git status --short | sed 's/^/  dirty: /'

echo "=== 1. the defconfig as a starting point ==="
make -C $T O=$OUT ARCH=arm CROSS_COMPILE=ccache\ arm-none-eabi- \
    twrp_hammerhead_defconfig > /tmp/opencode/romcfg.log 2>&1 || {
      tail -20 /tmp/opencode/romcfg.log; exit 1; }
echo "  ok"

echo "=== 2. the ROM's own config replaces it ==="
cp /tmp/opencode/rom-kconfig $OUT/.config
make -C $T O=$OUT ARCH=arm CROSS_COMPILE=ccache\ arm-none-eabi- \
    olddefconfig >> /tmp/opencode/romcfg.log 2>&1 || {
      tail -20 /tmp/opencode/romcfg.log; exit 1; }
echo "  ok"

echo "=== 3. what changed against the ROM's config ==="
grep -vE '^\s*#|^\s*$' $OUT/.config | sort > /tmp/opencode/cfg-romcfg.txt
diff /tmp/opencode/cfg-rom.txt /tmp/opencode/cfg-romcfg.txt | grep -E '^[<>]' | sed 's/^/  /' | head -30
echo "  --- differing lines: $(diff /tmp/opencode/cfg-rom.txt /tmp/opencode/cfg-romcfg.txt | grep -cE '^[<>]')"

echo "=== 4. the camera symbols ==="
grep -E '^CONFIG_(MSMB_CAMERA|IMX179|MT9M114B|MSM_CPP|MSM_CSID|MSM_CSIPHY|MSM_ISPIF|USB_G_ANDROID)=' $OUT/.config | sed 's/^/  /'

echo "=== 5. compile ==="
make -C $T O=$OUT ARCH=arm CROSS_COMPILE=ccache\ arm-none-eabi- \
    zImage-dtb -j$(nproc) >> /tmp/opencode/romcfg.log 2>&1 || {
      grep -E 'error|Error' /tmp/opencode/romcfg.log | head -20; exit 1; }
ls -la $OUT/arch/arm/boot/zImage-dtb | sed 's/^/  /'
cat $OUT/include/config/kernel.release | sed 's/^/  release: /'
