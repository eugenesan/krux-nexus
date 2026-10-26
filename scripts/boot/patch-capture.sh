#!/bin/bash
# Patch the debug kernel so that the ISP writes down whatever the camera
# HAL uploads to it (the "chromatrix"), into a file the shell can read.
set -e
R=/home/user/Develop/src/android-air-gapped
T=$R/standalone/vendor/android_kernel_lge_hammerhead
cd $T

echo "=== the reg_cfg_cmd struct ==="
grep -rn -A22 'struct msm_vfe_reg_cfg_cmd {' include/media/ | head -28 | sed 's/^/  /'

python3 - <<'PY'
p='drivers/media/platform/msm/camera_v2/isp/msm_isp_util.c'
s=open(p).read()
anchor = """	for (i = 0; i < proc_cmd->num_cfg; i++)
		msm_isp_send_hw_cmd(vfe_dev, &reg_cfg_cmd[i],
			cfg_data, proc_cmd->cmd_len);
"""
assert anchor in s, "anchor not found"
patch = anchor + """
	/* DEBUG: capture what the camera HAL uploaded to the ISP, so that
	 * our own imaging server can replay it without the vendor blob. */
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
			vfs_write(f, (const char __user *)hdr, sizeof(hdr), &pos);
			vfs_write(f, (const char __user *)reg_cfg_cmd,
				sizeof(struct msm_vfe_reg_cfg_cmd) *
					proc_cmd->num_cfg, &pos);
			vfs_write(f, (const char __user *)cfg_data, proc_cmd->cmd_len, &pos);
			filp_close(f, NULL);
			pr_err("DEBUG: captured %s (%u blocks, %u bytes)\\n",
				path, proc_cmd->num_cfg, proc_cmd->cmd_len);
		}
	}
"""
s = s.replace(anchor, patch, 1)
open(p,'w').write(s)
print("  patched msm_isp_util.c")
PY

echo
echo "=== build ==="
DEFCONFIG_NAME=twrp_hammerhead_defconfig TOOLCHAIN=armgnu \
  bash $R/build/build-kernel.sh --tree $T --out /tmp/opencode/ko-dbg \
  > /tmp/opencode/kodbg.log 2>&1
grep -E '^\s+(ok|!!)' /tmp/opencode/kodbg.log | tail -3 | sed 's/^/  /'

echo
echo "=== pack ==="
W=/tmp/opencode/kcmp
cp /tmp/opencode/ko-dbg/arch/arm/boot/zImage-dtb $W/romandroid/kernel
python3 $R/standalone/rootfs/bootimg.py pack $W/romandroid $W/romandroid/ramdisk \
    $R/standalone/rootfs/stock/boot-ourkern-dbg-android.img | sed 's/^/  /'
