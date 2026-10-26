#!/bin/bash
# Fix the two remaining problems with the capture:
#
#  1. the CPP: the driver overwrites new_frame->cpp_cmd_msg with its own
#     kernel copy just before our block, so by the time we run, the
#     pointer is a kernel address and vfs_write() cannot copy from it.
#     Save the user's pointer before the overwrite.
#  2. the header: it lives in kernel memory, and this kernel's static
#     data is above TASK_SIZE, so copy_from_user() cannot reach it
#     either.  Put the metadata in the file name instead.
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
old = """\t\tstatic u32 dump_idx;
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
"""
new = """\t\tstatic u32 dump_idx;
\t\tstatic char path[80];
\t\tstruct file *f;
\t\tloff_t pos = 0;
\t\tconst struct cred *old_cred;

\t\t/* the metadata goes in the name: a header would have to be
\t\t * written out of kernel memory, and vfs_write() copies with
\t\t * copy_from_user(), which cannot reach it from here */
\t\tsnprintf(path, sizeof(path),
\t\t\t"/data/local/tmp/isp_cfg_%u_n%u_l%u.bin", dump_idx++,
\t\t\tproc_cmd->num_cfg, proc_cmd->cmd_len);
"""
assert old in s, "isp head"
s = s.replace(old, new, 1)

old = """\t\t} else {
\t\t\tint w1 = vfs_write(f, (const char __user *)hdr,
\t\t\t\tsizeof(hdr), &pos);
\t\t\tint w2 = vfs_write(f, (const char __user *)proc_cmd->cfg_cmd,
"""
new = """\t\t} else {
\t\t\tint w2 = vfs_write(f, (const char __user *)proc_cmd->cfg_cmd,
"""
assert old in s, "isp w1"
s = s.replace(old, new, 1)
s = s.replace("""\t\t\tpr_err("DEBUG: captured %s (%u blocks, %u bytes; wrote %d %d %d)\\n",
\t\t\t\tpath, proc_cmd->num_cfg, proc_cmd->cmd_len, w1, w2, w3);""",
"""\t\t\tpr_err("DEBUG: captured %s (%u blocks, %u bytes; wrote %d %d)\\n",
\t\t\t\tpath, proc_cmd->num_cfg, proc_cmd->cmd_len, w2, w3);""", 1)
open(ISP, 'w').write(s)
print("  isp capture: metadata in the name")

# ----------------------------------------------------------------- the CPP
s = open(CPP).read()
old = """\tnew_frame->cpp_cmd_msg = cpp_frame_msg;
"""
new = """\t/* DEBUG: keep the user's pointer: the line below replaces it
\t * with the kernel copy, which vfs_write() cannot copy from. */
\tuser_msg = new_frame->cpp_cmd_msg;

\tnew_frame->cpp_cmd_msg = cpp_frame_msg;
"""
assert old in s, "cpp assign"
s = s.replace(old, new, 1)

# declare user_msg
old = """\tstruct msm_cpp_frame_info_t *u_frame_info =
\t\t(struct msm_cpp_frame_info_t *)ioctl_ptr->ioctl_ptr;
"""
new = """\tstruct msm_cpp_frame_info_t *u_frame_info =
\t\t(struct msm_cpp_frame_info_t *)ioctl_ptr->ioctl_ptr;
\t/* DEBUG: the user's chromatrix message, before the driver
\t * replaces it with its own kernel copy */
\tvoid __user *user_msg = NULL;
"""
assert old in s, "cpp decl"
s = s.replace(old, new, 1)

old = """\t\tstatic u32 dump_idx;
\t\tstatic char path[80];
\t\tstatic u32 hdr[2];
\t\tstruct file *f;
\t\tloff_t pos = 0;
\t\tconst struct cred *old_cred;

\t\thdr[0] = new_frame->msg_len;
\t\thdr[1] = new_frame->frame_type;
\t\tsnprintf(path, sizeof(path),
\t\t\t"/data/local/tmp/cpp_cfg_%u.bin", dump_idx++);
"""
new = """\t\tstatic u32 dump_idx;
\t\tstatic char path[80];
\t\tstruct file *f;
\t\tloff_t pos = 0;
\t\tconst struct cred *old_cred;

\t\tsnprintf(path, sizeof(path),
\t\t\t"/data/local/tmp/cpp_cfg_%u_len%u.bin", dump_idx++,
\t\t\tnew_frame->msg_len);
"""
assert old in s, "cpp head"
s = s.replace(old, new, 1)

old = """\t\t} else {
\t\t\tint w1 = vfs_write(f, (const char __user *)hdr,
\t\t\t\tsizeof(hdr), &pos);
\t\t\t/* cpp_cmd_msg is the user's buffer -- the one
\t\t\t * copy_from_user() can reach */
\t\t\tint w2 = vfs_write(f,
\t\t\t\t(const char __user *)new_frame->cpp_cmd_msg,
\t\t\t\tsizeof(uint32_t) * new_frame->msg_len, &pos);
"""
new = """\t\t} else {
\t\t\t/* user_msg is the user's buffer -- the one
\t\t\t * copy_from_user() can reach */
\t\t\tint w2 = vfs_write(f, (const char __user *)user_msg,
\t\t\t\tsizeof(uint32_t) * new_frame->msg_len, &pos);
"""
assert old in s, "cpp writes"
s = s.replace(old, new, 1)
s = s.replace("""\t\t\tpr_err("DEBUG: captured %s (%u words; wrote %d %d)\\n",
\t\t\t\tpath, new_frame->msg_len, w1, w2);""",
"""\t\t\tpr_err("DEBUG: captured %s (%u words; wrote %d)\\n",
\t\t\t\tpath, new_frame->msg_len, w2);""", 1)
open(CPP, 'w').write(s)
print("  cpp capture: user pointer kept, metadata in the name")
PY

git add -A
git -c user.email=dev@local -c user.name=dev commit -q -m "debug: capture the chromatrix from the user's buffer

Two fixes to the capture:

  - the CPP driver replaces new_frame->cpp_cmd_msg with its own kernel
    copy before the capture runs, so the pointer we wrote out was a
    kernel address and copy_from_user() refused it (EFAULT).  Keep the
    user's pointer.
  - the header (the counts) has to be written out of kernel memory,
    which copy_from_user() cannot reach from here either, so it goes
    into the file name instead."
git log --oneline -1 | sed 's/^/  /'
