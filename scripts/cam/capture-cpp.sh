#!/bin/bash
# Capture the chromatrix: the ROM's camera HAL sends it to the CPP as a
# frame message (struct msm_cpp_frame_info_t::cpp_cmd_msg).
set -e
R=/home/user/Develop/src/android-air-gapped
T=$R/standalone/vendor/android_kernel_lge_hammerhead
W=/tmp/opencode/kcmp
cd $T

echo "=== patch msm_cpp_cfg() to write the message down ==="
python3 - <<'PY'
p='drivers/media/platform/msm/camera_v2/pproc/cpp/msm_cpp.c'
s=open(p).read()
anchor = """	new_frame->cpp_cmd_msg = cpp_frame_msg;
"""
assert anchor in s, "anchor not found"
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
			vfs_write(f, (const char __user *)hdr, sizeof(hdr), &pos);
			vfs_write(f, (const char __user *)cpp_frame_msg,
				sizeof(uint32_t) * new_frame->msg_len, &pos);
			filp_close(f, NULL);
			pr_err("DEBUG: captured %s (%u words)\\n",
				path, new_frame->msg_len);
		}
	}
"""
s = s.replace(anchor, patch, 1)
open(p,'w').write(s)
print("  patched msm_cpp.c")
PY

echo
echo "=== build ==="
DEFCONFIG_NAME=twrp_hammerhead_defconfig TOOLCHAIN=arch PROFILE=/tmp/opencode/nostrip.list \
  bash $R/build/build-kernel.sh --tree $T --out /tmp/opencode/ko-diff3 \
  > /tmp/opencode/kodiff3.log 2>&1
grep -E '^\s+(ok|!!)' /tmp/opencode/kodiff3.log | tail -3 | sed 's/^/  /'
grep -cE 'error:' /tmp/opencode/ko-diff3.build.log | sed 's/^/  errors: /'

echo
echo "=== pack and boot ==="
cp /tmp/opencode/ko-diff3/arch/arm/boot/zImage-dtb $W/romandroid/kernel
python3 $R/standalone/rootfs/bootimg.py pack $W/romandroid $W/romandroid/ramdisk \
    $R/standalone/rootfs/stock/boot-ourkern-diff3-android.img | sed 's/^/  /'
cd $R
timeout 30 adb reboot bootloader 2>&1 | tr -d '\r' || true
for i in $(seq 1 40); do
  if timeout 8 fastboot devices 2>/dev/null | grep -q fastboot; then
    echo "  in fastboot after ${i}s"; break
  fi
  sleep 1
done
timeout 240 fastboot boot standalone/rootfs/stock/boot-ourkern-diff3-android.img 2>&1 | tail -2 | sed 's/^/  /'
timeout 300 adb wait-for-device 2>&1 | sed 's/^/  /'
timeout 30 adb shell 'cat /proc/version' 2>&1 | tr -d '\r' | head -1 | sed 's/^/  /'
