#!/bin/bash
# Two more debug aids:
#   1. the kernel puts SELinux in permissive mode, so the shell can read
#      the kernel log and the camera daemon can write to /data/local/tmp;
#   2. the capture blocks report what they managed to do.
set -e
R=/home/user/Develop/src/android-air-gapped
T=$R/standalone/vendor/android_kernel_lge_hammerhead
cd $T
git checkout -q airgap/debug-capture

python3 - <<'PY'
# ---- 1. permissive SELinux -------------------------------------------
p = 'init/main.c'
s = open(p).read()
anchor = "static noinline void __init_refok rest_init(void)\n"
assert anchor in s, "anchor"
add = """/*
 * DEBUG (temporary): run SELinux permissive.
 *
 * The ROM's init switches SELinux back to enforcing, so on this kernel
 * the shell cannot read the kernel log and the camera daemon cannot
 * create the file the capture wants to write.  Both are needed to find
 * out what the camera HAL uploads, so for the diagnostic build the
 * kernel keeps SELinux permissive.
 */
static int __init debug_selinux_permissive(void)
{
\textern int selinux_enforcing;

\tselinux_enforcing = 0;
\tprintk(KERN_INFO "DEBUG: SELinux set to permissive\\n");
\treturn 0;
}
late_initcall(debug_selinux_permissive);

"""
s = s.replace(anchor, add + anchor, 1)
open(p, 'w').write(s)
print("  init/main.c: permissive SELinux")

# ---- 2. diagnostics in the capture blocks ------------------------------
ISP = 'drivers/media/platform/msm/camera_v2/isp/msm_isp_util.c'
CPP = 'drivers/media/platform/msm/camera_v2/pproc/cpp/msm_cpp.c'

s = open(ISP).read()
old = """\t\tf = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
\t\tif (!IS_ERR(f)) {
\t\t\tvfs_write(f, (const char __user *)hdr, sizeof(hdr), &pos);
\t\t\tvfs_write(f, (const char __user *)proc_cmd->cfg_cmd,
\t\t\t\tsizeof(struct msm_vfe_reg_cfg_cmd) *
\t\t\t\t\tproc_cmd->num_cfg, &pos);
\t\t\tvfs_write(f, (const char __user *)proc_cmd->cfg_data,
\t\t\t\tproc_cmd->cmd_len, &pos);
\t\t\tfilp_close(f, NULL);
\t\t\tpr_err("DEBUG: captured %s (%u blocks, %u bytes)\\n",
\t\t\t\tpath, proc_cmd->num_cfg, proc_cmd->cmd_len);
\t\t}
"""
new = """\t\tf = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
\t\tif (IS_ERR(f)) {
\t\t\tpr_err("DEBUG: capture failed: %s (%d)\\n",
\t\t\t\tpath, (int)PTR_ERR(f));
\t\t} else {
\t\t\tint w1 = vfs_write(f, (const char __user *)hdr,
\t\t\t\tsizeof(hdr), &pos);
\t\t\tint w2 = vfs_write(f, (const char __user *)proc_cmd->cfg_cmd,
\t\t\t\tsizeof(struct msm_vfe_reg_cfg_cmd) *
\t\t\t\t\tproc_cmd->num_cfg, &pos);
\t\t\tint w3 = vfs_write(f, (const char __user *)proc_cmd->cfg_data,
\t\t\t\tproc_cmd->cmd_len, &pos);
\t\t\tfilp_close(f, NULL);
\t\t\tpr_err("DEBUG: captured %s (%u blocks, %u bytes; wrote %d %d %d)\\n",
\t\t\t\tpath, proc_cmd->num_cfg, proc_cmd->cmd_len, w1, w2, w3);
\t\t}
"""
assert old in s, "isp block"
s = s.replace(old, new, 1)
open(ISP, 'w').write(s)
print("  isp capture: diagnostics")

s = open(CPP).read()
old = """\t\tf = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
\t\tif (!IS_ERR(f)) {
\t\t\tvfs_write(f, (const char __user *)hdr, sizeof(hdr), &pos);
\t\t\t/* cpp_cmd_msg is the user's buffer -- the one
\t\t\t * copy_from_user() can reach */
\t\t\tvfs_write(f, (const char __user *)new_frame->cpp_cmd_msg,
\t\t\t\tsizeof(uint32_t) * new_frame->msg_len, &pos);
\t\t\tfilp_close(f, NULL);
\t\t\tpr_err("DEBUG: captured %s (%u words)\\n",
\t\t\t\tpath, new_frame->msg_len);
\t\t}
"""
new = """\t\tf = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
\t\tif (IS_ERR(f)) {
\t\t\tpr_err("DEBUG: capture failed: %s (%d)\\n",
\t\t\t\tpath, (int)PTR_ERR(f));
\t\t} else {
\t\t\tint w1 = vfs_write(f, (const char __user *)hdr,
\t\t\t\tsizeof(hdr), &pos);
\t\t\t/* cpp_cmd_msg is the user's buffer -- the one
\t\t\t * copy_from_user() can reach */
\t\t\tint w2 = vfs_write(f,
\t\t\t\t(const char __user *)new_frame->cpp_cmd_msg,
\t\t\t\tsizeof(uint32_t) * new_frame->msg_len, &pos);
\t\t\tfilp_close(f, NULL);
\t\t\tpr_err("DEBUG: captured %s (%u words; wrote %d %d)\\n",
\t\t\t\tpath, new_frame->msg_len, w1, w2);
\t\t}
"""
assert old in s, "cpp block"
s = s.replace(old, new, 1)
open(CPP, 'w').write(s)
print("  cpp capture: diagnostics")
PY

git add -A
git -c user.email=dev@local -c user.name=dev commit -q -m "debug: permissive SELinux and capture diagnostics

The ROM's init switches SELinux back to enforcing, so on this kernel
the shell cannot read the kernel log (dmesg: permission denied) and the
camera daemon cannot create the file the capture wants to write.  For
the diagnostic build the kernel keeps SELinux permissive, and the
capture blocks now report the error code and the number of bytes they
managed to write."
git log --oneline -1 | sed 's/^/  /'
