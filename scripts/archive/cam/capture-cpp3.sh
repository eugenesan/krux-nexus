#!/bin/bash
# Rebuild the capture patch cleanly on top of master: one copy, both the
# ISP and the CPP, with set_fs(KERNEL_DS) so the kernel-space buffers
# are actually written out.
set -e
R=/home/user/Develop/src/android-air-gapped
T=$R/standalone/vendor/android_kernel_lge_hammerhead
W=/tmp/opencode/kcmp
cd $T

git checkout -q master
git checkout -q -B airgap/debug-capture

python3 - <<'PY'
ISP='drivers/media/platform/msm/camera_v2/isp/msm_isp_util.c'
CPP='drivers/media/platform/msm/camera_v2/pproc/cpp/msm_cpp.c'

# ---------------- the ISP ----------------
s = open(ISP).read()
anchor = """	for (i = 0; i < proc_cmd->num_cfg; i++)
		msm_isp_send_hw_cmd(vfe_dev, &reg_cfg_cmd[i],
			cfg_data, proc_cmd->cmd_len);
"""
assert anchor in s, "isp anchor"
patch = anchor + """
	/* DEBUG: capture what the camera HAL uploaded to the ISP. */
	{
		static u32 dump_idx;
		struct file *f;
		char path[80];
		loff_t pos = 0;
		u32 hdr[2];

		hdr[0] = proc_cmd->num_cfg;
		hdr[1] = proc_cmd->cmd_len;
		snprintf(path, sizeof(path),
			"/data/local/tmp/isp_cfg_%u.bin", dump_idx++);
		f = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (f) {
			set_fs(KERNEL_DS);
			vfs_write(f, (const char __user *)hdr, sizeof(hdr), &pos);
			vfs_write(f, (const char __user *)reg_cfg_cmd,
				sizeof(struct msm_vfe_reg_cfg_cmd) *
					proc_cmd->num_cfg, &pos);
			vfs_write(f, (const char __user *)cfg_data,
				proc_cmd->cmd_len, &pos);
			set_fs(USER_DS);
			filp_close(f, NULL);
			pr_err("DEBUG: captured %s (%u blocks, %u bytes)\\n",
				path, proc_cmd->num_cfg, proc_cmd->cmd_len);
		}
	}
"""
s = s.replace(anchor, patch, 1)
if '#include <linux/uaccess.h>' not in s:
    s = s.replace('#include <linux/fs.h>', '#include <linux/fs.h>\n#include <linux/uaccess.h>', 1)
open(ISP, 'w').write(s)
print("  isp capture added")

# ---------------- the CPP ----------------
s = open(CPP).read()
anchor = """	new_frame->cpp_cmd_msg = cpp_frame_msg;
"""
assert anchor in s, "cpp anchor"
patch = anchor + """
	/* DEBUG: capture the chromatrix the camera HAL sent to the CPP. */
	{
		static u32 dump_idx;
		struct file *f;
		char path[80];
		loff_t pos = 0;
		u32 hdr[2];

		hdr[0] = new_frame->msg_len;
		hdr[1] = new_frame->frame_type;
		snprintf(path, sizeof(path),
			"/data/local/tmp/cpp_cfg_%u.bin", dump_idx++);
		f = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (f) {
			set_fs(KERNEL_DS);
			vfs_write(f, (const char __user *)hdr, sizeof(hdr), &pos);
			vfs_write(f, (const char __user *)cpp_frame_msg,
				sizeof(uint32_t) * new_frame->msg_len, &pos);
			set_fs(USER_DS);
			filp_close(f, NULL);
			pr_err("DEBUG: captured %s (%u words)\\n",
				path, new_frame->msg_len);
		}
	}
"""
s = s.replace(anchor, patch, 1)
if '#include <linux/uaccess.h>' not in s:
    s = s.replace('#include <linux/fs.h>', '#include <linux/fs.h>\n#include <linux/uaccess.h>', 1)
open(CPP, 'w').write(s)
print("  cpp capture added")
PY

git add -A
git -c user.email=dev@local -c user.name=dev commit -q -m "debug: capture what the camera HAL uploads (ISP and CPP)

The camera2 stack's configuration is sent in two places:

  - the ISP: VIDIOC_MSM_VFE_REG_CFG, a list of reg_cfg_cmd blocks plus
    a data blob,
  - the CPP: VIDIOC_MSM_CPP_CFG, the chromatix as a frame message
    (struct msm_cpp_frame_info_t::cpp_cmd_msg, a u32 array whose last
    word is MSM_CPP_MSG_ID_TRAILER).

Both are written to /data/local/tmp so they can be lifted and carried by
our own imaging server instead of coming from the vendor HAL.

The data is in kernel memory and 3.4 has no kernel_write(), so the
writes are done under set_fs(KERNEL_DS)."
git log --oneline -2 | sed 's/^/  /'
grep -c 'DEBUG: captured' drivers/media/platform/msm/camera_v2/isp/msm_isp_util.c drivers/media/platform/msm/camera_v2/pproc/cpp/msm_cpp.c | sed 's/^/  /'

echo
echo "=== build ==="
DEFCONFIG_NAME=twrp_hammerhead_defconfig TOOLCHAIN=arch PROFILE=/tmp/opencode/nostrip.list \
  bash $R/build/build-kernel.sh --tree $T --out /tmp/opencode/ko-diff5 \
  > /tmp/opencode/kodiff5.log 2>&1
grep -E '^\s+(ok|!!)' /tmp/opencode/kodiff5.log | tail -3 | sed 's/^/  /'
grep -cE 'error:' /tmp/opencode/ko-diff5.build.log | sed 's/^/  errors: /'

echo
echo "=== pack and boot ==="
cp /tmp/opencode/ko-diff5/arch/arm/boot/zImage-dtb $W/romandroid/kernel
python3 $R/standalone/rootfs/bootimg.py pack $W/romandroid $W/romandroid/ramdisk \
    $R/standalone/rootfs/stock/boot-ourkern-diff5-android.img | sed 's/^/  /'
cd $R
timeout 30 adb reboot bootloader 2>&1 | tr -d '\r' || true
for i in $(seq 1 40); do
  if timeout 8 fastboot devices 2>/dev/null | grep -q fastboot; then
    echo "  in fastboot after ${i}s"; break
  fi
  sleep 1
done
timeout 240 fastboot boot standalone/rootfs/stock/boot-ourkern-diff5-android.img 2>&1 | tail -2 | sed 's/^/  /'
timeout 300 adb wait-for-device 2>&1 | sed 's/^/  adb up /'
timeout 30 adb shell 'cat /proc/version' 2>&1 | tr -d '\r' | head -1 | sed 's/^/  /'
