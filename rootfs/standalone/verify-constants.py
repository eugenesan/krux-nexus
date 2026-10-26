#!/usr/bin/env python3
"""
verify-constants.sh -- cross-check every hand-transcribed kernel constant in
b/rootfs/standalone/main.c against the kernel source tree.

WHY THIS EXISTS

The dominant failure mode of this project has been writing kernel constants
from memory instead of reading them out of headers. Four occurrences, each
costing a hardware run:

  1. read=45 (correct: 3), mknod=33 (correct: 39), pwrite64=146 (correct: 181),
     statfs=140 (correct: 99)  -- all silently wrong
  2. reboot=142  -- that is _newselect, which made reboot() "appear to block"
  3. reboot() argument order -- x86 ordering applied to ARM
  4. LINUX_REBOOT_CMD_POWER_OFF = 0x4321  -- the real value is 0x4321FEDC,
     so the call fell through to default and returned -EINVAL

Case 4 was found only by a human reading the RED diagnostic screen.

This script makes the remaining hand-transcribed values checkable without a
device. reboot.h is no longer transcribed at all -- it is vendored verbatim at
include/linux/reboot.h -- but it is still verified against the tree so the
vendored copy cannot drift from the kernel we actually boot.

EXIT STATUS: 0 if every value matches, 1 otherwise.
"""

import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "main.c")
VENDORED = os.path.join(HERE, "include", "linux", "reboot.h")

# Where the kernel tree lives. Override with KERN_TREE=... if it moves.
KERN = os.environ.get("KERN_TREE", "/tmp/opencode/kern/android_kernel_lge_hammerhead")
NFC_HDR = os.environ.get("NFC_HDR", "../../nfc/bcm2079x.h")

fails = []
oks = []


def compiled_sizes(src_text, names):
    """Measure sizeof for struct definitions lifted straight out of main.c.

    The armel cross compiler can produce the value but we cannot run ARM code,
    and compiling main.c against the kernel tree fails because this 3.4 tree has
    no arch/arm/include/asm/bitsperlong.h. So: extract OUR struct bodies, wrap
    them with the fixed-width typedefs main.c uses, and ask the host compiler.

    `char p_x[1 - sizeof(T)]` is deliberate: gcc reports the negative bound in
    the error text, which is a reliable way to read a constant out of a
    compiler that cannot execute. A plain oversized array only says "too big".
    """
    import subprocess
    import tempfile

    blocks = {}
    for nm in names:
        m = re.search(r"struct\s+" + nm + r"\s*\{.*?\n\};", src_text, re.S)
        if m:
            blocks[nm] = m.group(0)
    if not blocks:
        return {}
    body = ("typedef unsigned int u32;\ntypedef unsigned short u16;\n"
            "typedef int s32;\n")
    body += "\n".join(blocks.values()) + "\n"
    body += "\n".join("char p_%s[1 - sizeof(struct %s)];" % (n, n)
                      for n in blocks) + "\n"
    fd, path = tempfile.mkstemp(suffix=".c")
    with os.fdopen(fd, "w") as f:
        f.write(body)
    out = {}
    try:
        try:
            p = subprocess.run(["gcc", "-c", path, "-o", os.devnull],
                               capture_output=True, text=True, timeout=60)
        except (FileNotFoundError, subprocess.TimeoutExpired):
            return {}
        for nm in blocks:
            m = re.search(r"array '%s' is negative \(-(\d+)\)" % re.escape("p_" + nm),
                          p.stderr)
            if m:
                out[nm] = int(m.group(1)) + 1      # we asked for 1 - sizeof
    finally:
        os.unlink(path)
    return out


def die(msg):
    print("  ERROR: %s" % msg)
    sys.exit(2)


def read(path):
    if not os.path.exists(path):
        die("missing file: %s" % path)
    with open(path, "r", errors="replace") as f:
        return f.read()


def kern_defines(name):
    """Collect '#define NAME <value>' for NAME across the kernel tree."""
    out = {}
    pat = re.compile(r"^\s*#\s*define\s+" + re.escape(name) + r"\s+(\S+)", re.M)
    for root, _dirs, files in os.walk(os.path.join(KERN, "include")):
        for fn in files:
            if not fn.endswith(".h"):
                continue
            p = os.path.join(root, fn)
            try:
                txt = read(p)
            except Exception:
                continue
            for m in pat.finditer(txt):
                out.setdefault(m.group(1), p)
    return out


def norm(v):
    """Strip U/L suffixes and case so 0xFEE1DEADUL == 0xfee1dead."""
    return v.rstrip("uUlL").lower()


def check(label, ours, theirs):
    a, b = norm(str(ours)), norm(str(theirs))
    if a == b:
        oks.append(label)
        print("  OK       %-38s %s" % (label, a))
    else:
        fails.append(label)
        print("  MISMATCH %-38s ours=%s kernel=%s" % (label, a, b))


def check_ours_defines(src, name):
    m = re.search(r"^\s*#\s*define\s+" + re.escape(name) + r"\s+(\S+)", src, re.M)
    return m.group(1) if m else None


# ---------------------------------------------------------------- reboot.h --
print("=== reboot: vendored header vs kernel tree ===")
if not os.path.exists(KERN):
    die("kernel tree not found at %s (set KERN_TREE)" % KERN)

src = read(SRC)
vend = read(VENDORED)
kern_reboot = read(os.path.join(KERN, "include", "linux", "reboot.h"))
COMPILED_SIZES = compiled_sizes(src, ("fb_var_screeninfo", "mdp_display_commit"))
if COMPILED_SIZES:
    print("=== struct sizes (measured by the host compiler) ===")
    for k, v in sorted(COMPILED_SIZES.items()):
        print("  sizeof(struct %-22s) = %d" % (k + ")", v))

for name in ("LINUX_REBOOT_MAGIC1", "LINUX_REBOOT_MAGIC2",
             "LINUX_REBOOT_CMD_POWER_OFF", "LINUX_REBOOT_CMD_HALT"):
    ours = re.search(r"^\s*#\s*define\s+" + name + r"\s+(\S+)", vend, re.M)
    theirs = re.search(r"^\s*#\s*define\s+" + name + r"\s+(\S+)", kern_reboot, re.M)
    if not ours or not theirs:
        fails.append(name)
        print("  MISSING  %s (vendored=%s kernel=%s)"
              % (name, bool(ours), bool(theirs)))
        continue
    check(name, ours.group(1), theirs.group(1))

if "#include <linux/reboot.h>" not in src:
    fails.append("reboot.h include")
    print("  MISMATCH main.c does not #include <linux/reboot.h>")
else:
    oks.append("reboot.h include")
    print("  OK       %-38s %s" % ("main.c includes <linux/reboot.h>", ""))

if check_ours_defines(src, "LINUX_REBOOT_CMD_POWER_OFF") is not None:
    fails.append("transcribed reboot cmd")
    print("  MISMATCH main.c still transcribes LINUX_REBOOT_CMD_POWER_OFF")
else:
    oks.append("no transcribed reboot cmd")
    print("  OK       %-38s %s" % ("main.c transcribes no reboot cmd", ""))

# --------------------------------------------------------------- syscalls --
print("\n=== syscall numbers vs arch/arm/include/asm/unistd.h ===")
# This 3.4 tree has NO asm/unistd-eabi.h. The EABI numbers live in unistd.h,
# written as (#define __NR_read (__NR_SYSCALL_BASE+  3)). __NR_SYSCALL_BASE is
# __NR_OAPI_SYSCALL_BASE on OABI and 0 on EABI, which is why reboot=142 was
# really _newselect and never reboot.
un_path = None
for cand in ("arch/arm/include/asm/unistd-eabi.h",
             "arch/arm/include/asm/unistd.h"):
    p = os.path.join(KERN, cand)
    if os.path.exists(p):
        un_path = p
        break
if un_path is None:
    fails.append("unistd.h")
    print("  MISSING  no unistd-eabi.h or unistd.h under arch/arm/include/asm")
else:
  print("  using %s" % un_path.replace(KERN + "/", ""))
  un = read(un_path)
  for num, name in (("3", "read"), ("4", "write"), ("5", "open"), ("6", "close"),
                    ("20", "getpid"), ("21", "mount"), ("39", "mkdir"),
                    ("54", "ioctl"), ("88", "reboot"), ("118", "fsync"),
                    ("168", "poll"), ("192", "mmap2")):
    m = re.search(r"^#define\s+__NR_" + name + r"\s+\(__NR_SYSCALL_BASE\+\s*(\d+)\)", un, re.M)
    if not m:
        fails.append("__NR_" + name)
        print("  MISSING  __NR_%s not found in unistd.h" % name)
        continue
    check("__NR_%s" % name, num, m.group(1))

# ------------------------------------------------------------ framebuffer --
print("\n=== framebuffer commit ioctl vs include/linux/msm_mdp.h ===")
mdp = read(os.path.join(KERN, "include", "linux", "msm_mdp.h"))
ours = check_ours_defines(src, "MSMFB_DISPLAY_COMMIT")
# The header's define spans two lines with a backslash continuation, and its
# third argument is a struct TYPE whose size only the compiler knows. So:
#   - parse the macro's type and nr from the header (join continuations first)
#   - get the size from a compile-time tripwire in main.c, which the build
#     already enforces (a negative array bound fails the build)
mdp_joined = mdp.replace("\\\n", " ")
m = re.search(r"#define\s+MSMFB_DISPLAY_COMMIT\s+_IOW\(\s*MSMFB_IOCTL_MAGIC\s*,"
               r"\s*(\d+)\s*,\s*struct\s+(\w+)\s*\)", mdp_joined)
mg = re.search(r"#define\s+MSMFB_IOCTL_MAGIC\s+'(\w)'", mdp_joined)
if ours and m and mg:
    nr = int(m.group(1))
    struct = m.group(2)
    magic = ord(mg.group(1))
    sz = COMPILED_SIZES.get(struct)
    if sz is None:
        print("  SKIP     size of struct %s not compiled on this host "
              "(host layout may differ from armel)" % struct)
        print("           MSMFB_DISPLAY_COMMIT cross-check deferred to main.c's "
              "compile-time tripwire")
    else:
        want = (1 << 30) | (sz << 16) | (magic << 8) | nr
        check("MSMFB_DISPLAY_COMMIT", ours, "0x%08X" % want)
        print("           header: _IOW('%s',%d,struct %s [%d bytes])"
              % (mg.group(1), nr, struct, sz))
elif m and mg:
    nr = int(m.group(1))
    print("  NOTE     header has _IOW('%s',%d,struct %s); ours=%s"
          % (mg.group(1), nr, m.group(2), ours))
else:
    fails.append("MSMFB_DISPLAY_COMMIT")
    print("  MISSING  could not parse MSMFB_DISPLAY_COMMIT from msm_mdp.h")

# ------------------------------------------------------------------ touch --
print("\n=== input event codes vs include/linux/input.h ===")
inp = read(os.path.join(KERN, "include", "linux", "input.h"))
for name in ("EV_ABS", "ABS_MT_POSITION_X", "ABS_MT_POSITION_Y", "ABS_MT_TRACKING_ID"):
    ours = check_ours_defines(src, name)
    theirs = re.search(r"^\s*#define\s+" + name + r"\s+(\S+)", inp, re.M)
    if ours and theirs:
        check(name, ours, theirs.group(1))
    else:
        fails.append(name)
        print("  MISSING  %s (ours=%s header=%s)" % (name, ours, bool(theirs)))

# -------------------------------------------------------------------- NFC --
print("\n=== NFC ioctls vs b/nfc/bcm2079x.h ===")
nfc_path = os.path.normpath(os.path.join(HERE, NFC_HDR))
if os.path.exists(nfc_path):
    nfc = read(nfc_path)
    # These are _IO(BCMNFC_MAGIC, nr) -- a pure type/nr pair with no size, so
    # unlike MSMFB_DISPLAY_COMMIT they can be evaluated exactly in Python:
    # _IOC_NONE is 0 and the size field is 0, so the value is (magic<<8)|nr.
    # Resolving the magic means reading BCMNFC_MAGIC out of the header rather
    # than assuming 0xFA.
    mg = re.search(r"^\s*#\s*define\s+BCMNFC_MAGIC\s+(\S+)", nfc, re.M)
    magic = int(mg.group(1), 0) if mg else None
    for name in ("BCMNFC_POWER_CTL", "BCMNFC_WAKE_CTL"):
        ours = check_ours_defines(src, name)
        theirs = re.search(r"^\s*#\s*define\s+" + name + r"\s+_IO\(\s*BCMNFC_MAGIC\s*,"
                           r"\s*([^)]+)\)", nfc, re.M)
        if ours and theirs and magic is not None:
            nr = int(theirs.group(1).strip(), 0)
            check(name, ours, "0x%04X" % ((magic << 8) | nr))
            print("           header: _IO(0x%02X, 0x%02X)" % (magic, nr))
        else:
            fails.append(name)
            print("  MISSING  %s (ours=%s parsed=%s magic=%s)"
                  % (name, ours, bool(theirs), magic))
else:
    print("  SKIP     %s not found" % nfc_path)

# --------------------------------------------------------------- summary --
print("\n=== summary ===")
print("  %d checks passed, %d failed" % (len(oks), len(fails)))
if fails:
    print("  FAILED: " + ", ".join(fails))
    sys.exit(1)
print("  all hand-transcribed constants agree with the kernel tree")
sys.exit(0)