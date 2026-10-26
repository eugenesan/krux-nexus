#!/bin/bash
# Fix the capture patches: the buffers are in kernel space, so the writes
# need set_fs(KERNEL_DS) (3.4 has no kernel_write()).
set -e
R=/home/user/Develop/src/android-air-gapped
T=$R/standalone/vendor/android_kernel_lge_hammerhead
W=/tmp/opencode/kcmp
cd $T
git checkout -q airgap/debug-capture

python3 - <<'PY'
# --- the CPP capture ---
p='drivers/media/platform/msm/camera_v2/pproc/cpp/msm_cpp.c'
s=open(p).read()
old = """		if (f) {
			vfs_write(f, (const char __user *)hdr, sizeof(hdr), &pos);
			vfs_write(f, (const char __user *)cpp_frame_msg,
				sizeof(uint32_t) * new_frame->msg_len, &pos);
			filp_close(f, NULL);"""
new = """		if (f) {
			/* the data is in kernel memory: 3.4 has no
			 * kernel_write(), so let the copy come from
			 * kernel space for a moment */
			set_fs(KERNEL_DS);
			vfs_write(f, (const char __user *)hdr, sizeof(hdr), &pos);
			vfs_write(f, (const char __user *)cpp_frame_msg,
				sizeof(uint32_t) * new_frame->msg_len, &pos);
			set_fs(USER_DS);
			filp_close(f, NULL);"""
assert old in s, "cpp anchor not found"
s = s.replace(old, new, 1)
if '#include <linux/uaccess.h>' not in s:
    s = s.replace('#include <linux/fs.h>', '#include <linux/fs.h>\n#include <linux/uaccess.h>', 1)
open(p,'w').write(s)
print("  cpp patch fixed")

# --- the ISP capture ---
p='drivers/media/platform/msm/camera_v2/isp/msm_isp_util.c'
s=open(p).read()
old = """		if (f) {
			vfs_write(f, (const char __user *)hdr, sizeof(hdr), &pos);
			vfs_write(f, (const char __user *)reg_cfg_cmd,
				sizeof(struct msm_vfe_reg_cfg_cmd) *
					proc_cmd->num_cfg, &pos);
			vfs_write(f, (const char __user *)cfg_data, proc_cmd->cmd_len, &pos);
			filp_close(f, NULL);"""
new = """		if (f) {
			set_fs(KERNEL_DS);
			vfs_write(f, (const char __user *)hdr, sizeof(hdr), &pos);
			vfs_write(f, (const char __user *)reg_cfg_cmd,
				sizeof(struct msm_vfe_reg_cfg_cmd) *
					proc_cmd->num_cfg, &pos);
			vfs_write(f, (const char __user *)cfg_data, proc_cmd->cmd_len, &pos);
			set_fs(USER_DS);
			filp_close(f, NULL);"""
assert old in s, "isp anchor not found"
s = s.replace(old, new, 1)
if '#include <linux/uaccess.h>' not in s:
    s = s.replace('#include <linux/fs.h>', '#include <linux/fs.h>\n#include <linux/uaccess.h>', 1)
open(p,'w').write(s)
print("  isp patch fixed")
PY

git add -A
git -c user.email=dev@local -c user.name=dev commit -q -m "debug: write the captured buffers from kernel space

The captured data (the chromatrix message, the reg_cfg_cmd blocks) lives
in kernel memory.  3.4 has no kernel_write(), and vfs_write() copies from
user space, so the first version of these patches wrote nothing (EFAULT).
set_fs(KERNEL_DS) around the writes makes the copy come from kernel
space."
git log --oneline -1 | sed 's/^/  /'

echo
echo "=== build (the differential: no air-gap profile, the ROM's toolchain) ==="
DEFCONFIG_NAME=twrp_hammerhead_defconfig TOOLCHAIN=arch PROFILE=/tmp/opencode/nostrip.list \
  bash $R/build/build-kernel.sh --tree $T --out /tmp/opencode/ko-diff4 \
  > /tmp/opencode/kodiff4.log 2>&1
grep -E '^\s+(ok|!!)' /tmp/opencode/kodiff4.log | tail -3 | sed 's/^/  /'
grep -cE 'error:' /tmp/opencode/ko-diff4.build.log | sed 's/^/  errors: /'

echo
echo "=== pack and boot ==="
cp /tmp/opencode/ko-diff4/arch/arm/boot/zImage-dtb $W/romandroid/kernel
python3 $R/standalone/rootfs/bootimg.py pack $W/romandroid $W/romandroid/ramdisk \
    $R/standalone/rootfs/stock/boot-ourkern-diff4-android.img | sed 's/^/  /'
cd $R
timeout 30 adb reboot bootloader 2>&1 | tr -d '\r' || true
for i in $(seq 1 40); do
  if timeout 8 fastboot devices 2>/dev/null | grep -q fastboot; then
    echo "  in fastboot after ${i}s"; break
  fi
  sleep 1
done
timeout 240 fastboot boot standalone/rootfs/stock/boot-ourkern-diff4-android.img 2>&1 | tail -2 | sed 's/^/  /'
timeout 300 adb wait-for-device 2>&1 | sed 's/^/  adb up /'
timeout 30 adb shell 'cat /proc/version' 2>&1 | tr -d '\r' | head -1 | sed 's/^/  /'
