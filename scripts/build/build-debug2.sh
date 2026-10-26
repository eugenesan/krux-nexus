#!/bin/bash
# Build a debug kernel that (a) logs the camera_v2 internals and
# (b) lets an unprivileged user read the kernel log, so that the ROM's
# camera HAL can be watched from the shell.
set -e
R=/home/user/Develop/src/android-air-gapped
T=$R/standalone/vendor/android_kernel_lge_hammerhead
cd $T

echo "=== the printk defaults ==="
grep -n -E 'dmesg_restrict|log_buf_len =' kernel/printk.c | head -6 | sed 's/^/  /'

python3 - <<'PY'
p='kernel/printk.c'
s=open(p).read()
if 'dmesg_restrict = 0' not in s:
    s=s.replace("int dmesg_restrict = 1;", "int dmesg_restrict = 0;\t/* DEBUG: let the shell read the log */")
if 'log_buf_len = 16 * 1024 * 1024' not in s:
    s=s.replace("int log_buf_len __read_mostly = 1 << CONFIG_LOG_BUF_SHIFT;",
                "int log_buf_len __read_mostly = 16 * 1024 * 1024;\t/* DEBUG */")
open(p,'w').write(s)
print("  patched")
PY
grep -n -E 'dmesg_restrict = 0|log_buf_len __read_mostly = 16' kernel/printk.c | sed 's/^/  /'

echo
echo "=== the camera debug logging on (temporarily) ==="
cp $R/standalone/tools/profiles/hammerhead-camera.list /tmp/opencode/hammerhead-camera.list.bak
sed -i 's/^CONFIG_MSMB_CAMERA_DEBUG=n$/CONFIG_MSMB_CAMERA_DEBUG=y/' \
    $R/standalone/tools/profiles/hammerhead-camera.list

echo
echo "=== build ==="
DEFCONFIG_NAME=twrp_hammerhead_defconfig TOOLCHAIN=armgnu \
  bash $R/build/build-kernel.sh --tree $T --out /tmp/opencode/ko-dbg \
  > /tmp/opencode/kodbg.log 2>&1
grep -E '^\s+(ok|!!)' /tmp/opencode/kodbg.log | tail -3 | sed 's/^/  /'

echo
echo "=== restore the profile ==="
cp /tmp/opencode/hammerhead-camera.list.bak $R/standalone/tools/profiles/hammerhead-camera.list
grep -n 'MSMB_CAMERA_DEBUG' $R/standalone/tools/profiles/hammerhead-camera.list | sed 's/^/  /'

echo
echo "=== pack: debug kernel + the ROM's Android ramdisk ==="
W=/tmp/opencode/kcmp
rm -rf $W/romandroid && mkdir -p $W/romandroid
python3 $R/standalone/rootfs/bootimg.py unpack $R/standalone/rootfs/stock/boot.img $W/romandroid >/dev/null
cp /tmp/opencode/ko-dbg/arch/arm/boot/zImage-dtb $W/romandroid/kernel
python3 $R/standalone/rootfs/bootimg.py pack $W/romandroid $W/romandroid/ramdisk \
    $R/standalone/rootfs/stock/boot-ourkern-dbg-android.img | sed 's/^/  /'
