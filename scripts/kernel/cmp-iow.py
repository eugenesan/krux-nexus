#!/usr/bin/env python3
"""
cmp-iow.py -- compare the register accesses two kernels make in a function.

The camera fault is a register-level fault: the CSID's channel lookup
table is programmed for the wrong virtual channel.  The author's kernel
and ours come from the same source, but they run on the same hardware
with a different *result*, so the register writes must differ somewhere.

This script disassembles one function in both kernels, finds every call
to the camera's register accessors (msm_camera_io_w / _r), works out
the two arguments of each call (the value and the register offset), and
prints the two lists side by side.

The arguments are read out of the instructions before the call: the
value is usually an immediate or a register that was loaded from one,
the offset is an immediate added to the base.

Usage:
  cmp-iow.py <ours.vmlinux> <author.vmlinux.bin> <author-kallsyms> <func>
"""
import re
import subprocess
import sys

OBJDUMP = "/home/user/Develop/toolchains/arch-16.2.0/usr/bin/arm-none-eabi-objdump"


def run(cmd):
    return subprocess.run(cmd, shell=True, capture_output=True,
                          text=True).stdout


def load(path, ours=False):
    syms = {}
    if ours:
        for line in run("nm %s" % path).splitlines():
            p = line.split()
            if len(p) == 3 and p[1] in "tT":
                syms[p[2]] = int(p[0], 16)
    else:
        for line in open(path, errors="replace"):
            p = line.split()
            if len(p) == 3 and p[1] in "tT":
                try:
                    syms[p[2]] = int(p[0], 16)
                except ValueError:
                    pass
    return syms


def by_addr(syms):
    out = {}
    for n, a in syms.items():
        out.setdefault(a, n)
    return out


def dis_ours(elf, func):
    return run("%s -d --disassemble=%s %s" % (OBJDUMP, func, elf))


def dis_author(binpath, syms, func):
    if func not in syms:
        return ""
    start = syms[func]
    nxt = sorted(a for _, a in ((n, a) for n, a in syms.items() if a > start))[0]
    return run("%s -D -b binary -m arm --adjust-vma=0x%x --start-address=0x%x "
               "--stop-address=0x%x %s" % (OBJDUMP, syms["_text"], start, nxt,
                                           binpath))


REG_CALLS = ("msm_camera_io_w", "msm_camera_io_r", "msm_camera_io_w_mb",
             "msm_camera_io_r_mb")


def accesses(text, names):
    """the (accessor, value, offset) triples"""
    lines = text.splitlines()
    out = []
    regs = {}          # register -> the immediate it holds
    for line in lines:
        m = re.search(r"\b(mov|movw|movt|ldr|add|orr|adds|and|str)\b\s*"
                      r"(r\d+|r1[0-5])", line)
        if m:
            op, reg = m.group(1), m.group(2)
            im = re.search(r"#(0x[0-9a-fA-F]+|\d+)\b", line)
            if im:
                val = int(im.group(1), 0)
                if op in ("mov", "movw"):
                    regs[reg] = val
                elif op in ("orr", "and"):
                    regs[reg] = regs.get(reg, 0) | val
                elif op == "add" and reg not in regs:
                    regs[reg] = val
        call = re.search(r"\bbl\s+(?:0x)?([0-9a-f]+)\b", line)
        if call:
            name = names.get(int(call.group(1), 16), "")
            if name in REG_CALLS:
                # r0 = the value, r1 = the address (base + offset)
                val = regs.get("r0", "?")
                off = regs.get("r1", "?")
                out.append((name, val, off))
                regs = {}
    return out


def main():
    if len(sys.argv) < 5:
        print(__doc__)
        return 1
    ours_elf, author_bin, ksym_file, func = sys.argv[1:5]
    osyms, asyms = load(ours_elf, ours=True), load(ksym_file)
    onames, anames = by_addr(osyms), by_addr(asyms)

    o = dis_ours(ours_elf, func)
    a = dis_author(author_bin, asyms, func)
    if not o.strip() or not a.strip():
        print("  a kernel has no %s" % func)
        return 1

    oa, aa = accesses(o, onames), accesses(a, anames)
    print("== %s" % func)
    print("   the register accesses: ours %d, the author's %d" % (len(oa), len(aa)))
    n = max(len(oa), len(aa))
    for i in range(n):
        x = oa[i] if i < len(oa) else ("", "", "")
        y = aa[i] if i < len(aa) else ("", "", "")
        mark = "  " if x[1:] == y[1:] else "**"
        print("   %s %-20s val %-8s off %-8s | %-20s val %-8s off %s"
              % (mark, x[0].replace("msm_camera_io_", ""), x[1], x[2],
                 y[0].replace("msm_camera_io_", ""), y[1], y[2]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
