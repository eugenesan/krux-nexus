#!/bin/bash
# Rewrite the two capture blocks with the lessons learned:
#
#  - filp_open() returns ERR_PTR() on failure in this tree, so the check
#    must be IS_ERR(), not "if (f)".
#  - the camera daemon (mm-qcamerad) may not create files in
#    /data/local/tmp, so the file is opened as root (override_creds).
#  - vfs_write() copies with copy_from_user(), which on this ARM kernel
#    only accepts addresses below TASK_SIZE: the kernel image is, the
#    stack is not.  So anything the kernel itself has to write out has
#    to live in a static buffer.
#  - the data to capture must be read from the USER pointers
#    (proc_cmd->cfg_cmd / proc_cmd->cfg_data, new_frame->cpp_cmd_msg):
#    those are what copy_from_user() can reach.  The kernel-side copies
#    the driver made cannot be written out.
set -e
R=/home/user/Develop/src/android-air-gapped
T=$R/standalone/vendor/android_kernel_lge_hammerhead
cd $T
git checkout -q airgap/debug-capture

python3 - <<'PY'
ISP = 'drivers/media/platform/msm/camera_v2/isp/msm_isp_util.c'
CPP = 'drivers/media/platform/msm/camera_v2/pproc/cpp/msm_cpp.c'

# ----------------------------------------------------------------- the ISP
s = open(ISP).read()
start = s.index("\t/* DEBUG: capture what the camera HAL uploaded to the ISP. */")
end = s.index("\n\t}\n", s.index('pr_err("DEBUG: captured %s (%u blocks', start)) + len("\n\t}\n")
new = """\t/* DEBUG: capture what the camera HAL uploaded to the ISP. */
\t{
\t\tstatic u32 dump_idx;
\t\t/* static: the stack is above TASK_SIZE, and vfs_write()
\t\t * copies with copy_from_user(), which only reaches below
\t\t * TASK_SIZE -- the kernel image is, the stack is not. */
\t\tstatic char path[80];
\t\tstatic u32 hdr[2];
\t\tstruct file *f;
\t\tloff_t pos = 0;
\t\tconst struct cred *old_cred;

\t\thdr[0] = proc_cmd->num_cfg;
\t\thdr[1] = proc_cmd->cmd_len;
\t\tsnprintf(path, sizeof(path),
\t\t\t"/data/local/tmp/isp_cfg_%u.bin", dump_idx++);
\t\t/* the camera daemon is not allowed to create files in
\t\t * /data/local/tmp -- do it as root */
\t\told_cred = override_creds(&init_cred);
\t\tf = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
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
\t\trevert_creds(old_cred);
\t}
"""
s = s[:start] + new + s[end:]
if '#include <linux/cred.h>' not in s:
    s = s.replace('#include <linux/fs.h>', '#include <linux/fs.h>\n#include <linux/cred.h>', 1)
open(ISP, 'w').write(s)
print("  isp capture rewritten")

# ----------------------------------------------------------------- the CPP
s = open(CPP).read()
start = s.index("\t/* DEBUG: capture the chromatrix the camera HAL sent to the CPP. */")
end = s.index("\n\t}\n", s.index('pr_err("DEBUG: captured %s (%u words)', start)) + len("\n\t}\n")
new = """\t/* DEBUG: capture the chromatrix the camera HAL sent to the CPP. */
\t{
\t\tstatic u32 dump_idx;
\t\tstatic char path[80];
\t\tstatic u32 hdr[2];
\t\tstruct file *f;
\t\tloff_t pos = 0;
\t\tconst struct cred *old_cred;

\t\thdr[0] = new_frame->msg_len;
\t\thdr[1] = new_frame->frame_type;
\t\tsnprintf(path, sizeof(path),
\t\t\t"/data/local/tmp/cpp_cfg_%u.bin", dump_idx++);
\t\told_cred = override_creds(&init_cred);
\t\tf = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
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
\t\trevert_creds(old_cred);
\t}
"""
s = s[:start] + new + s[end:]
if '#include <linux/cred.h>' not in s:
    s = s.replace('#include <linux/fs.h>', '#include <linux/fs.h>\n#include <linux/cred.h>', 1)
open(CPP, 'w').write(s)
print("  cpp capture rewritten")
PY

git add -A
git -c user.email=dev@local -c user.name=dev commit -q -m "debug: fix the capture blocks

Three things were wrong with the first version, and the kernel said so
loudly (an oops in vfs_write, called from msm_isp_proc_cmd):

  - filp_open() returns ERR_PTR() on failure in this tree, so \"if (f)\"
    is not a test for success: it passed ERR_PTR(-EACCES) to vfs_write.
    The camera daemon is not allowed to create files in /data/local/tmp,
    so the file is now opened as root.
  - vfs_write() copies with copy_from_user(), which on this ARM kernel
    only reaches addresses below TASK_SIZE: the kernel image is below
    it, the stack is not.  Buffers the kernel has to write out are now
    static.
  - the data has to be read from the USER pointers, not from the copies
    the driver made in kernel space.

With this, the camera HAL's uploads are written to /data/local/tmp."
git log --oneline -1 | sed 's/^/  /'
