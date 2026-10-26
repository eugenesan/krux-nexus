#!/usr/bin/env python3
"""
cmp-callseq.py -- compare the *calls* two kernels make in the same function.

The author's ROM kernel and ours come from the same source tree but are
built with different compilers, so the instructions differ.  What does
not differ is the sequence of functions a piece of code calls, and the
constants it passes them: that is the behaviour.

So this script disassembles the same function in both kernels, resolves
every branch-with-link to the name of the function it calls, and prints
the two sequences next to each other.  A difference in the sequence is a
real difference in the code; a difference in a constant is a real
difference in what the code does.

Usage:
  cmp-callseq.py <ours.vmlinux> <author.vmlinux.bin> <author-kallsyms> <func>
"""
import re
import subprocess
import sys

OBJDUMP = "/home/user/Develop/toolchains/arch-16.2.0/usr/bin/arm-none-eabi-objdump"


def run(cmd):
    return subprocess.run(cmd, shell=True, capture_output=True,
                          text=True).stdout


def load_syms(path, ours=False):
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
    """address -> name (the first name at that address)"""
    out = {}
    for n, a in syms.items():
        out.setdefault(a, n)
    return out


def calls(text, names):
    """the sequence of (called function, immediates around the call)"""
    out = []
    lines = text.splitlines()
    for i, line in enumerate(lines):
        m = re.search(r"\bbl\s+(0x[0-9a-f]+|\S+)", line)
        if not m:
            continue
        tgt = m.group(1)
        if tgt.startswith("0x"):
            addr = int(tgt, 16)
        else:
            addr = None
        name = names.get(addr, tgt) if addr is not None else tgt
        # the immediates of the few instructions before the call
        imms = []
        for prev in lines[max(0, i - 6):i]:
            for im in re.finditer(r"#(0x[0-9a-fA-F]+|\d+)\b", prev):
                imms.append(im.group(1))
        out.append((name, imms[-3:]))
    return out


def main():
    if len(sys.argv) < 5:
        print(__doc__)
        return 1
    ours_elf, author_bin, ksym_file, func = sys.argv[1:5]

    osyms = load_syms(ours_elf, ours=True)
    asyms = load_syms(ksym_file)
    onames, anames = by_addr(osyms), by_addr(asyms)

    o = run("%s -d --disassemble=%s %s" % (OBJDUMP, func, ours_elf))
    if not o.strip():
        print("  our kernel has no %s" % func)
        return 1

    if func not in asyms:
        print("  the author's kernel has no %s" % func)
        return 1
    start = asyms[func]
    nxt = sorted(a for _, a in ((n, a) for n, a in asyms.items() if a > start))[0]
    a = run("%s -D -b binary -m arm --adjust-vma=0x%x --start-address=0x%x "
            "--stop-address=0x%x %s" % (OBJDUMP, asyms["_text"], start, nxt,
                                        author_bin))

    oc, ac = calls(o, onames), calls(a, anames)
    print("== %s" % func)
    print("   the calls: ours %d, the author's %d" % (len(oc), len(ac)))
    n = max(len(oc), len(ac))
    for i in range(n):
        oi = oc[i] if i < len(oc) else ("", [])
        ai = ac[i] if i < len(ac) else ("", [])
        mark = "  " if oi[0] == ai[0] else "**"
        print("   %s %-34s %-22s | %-34s %s" %
              (mark, oi[0], ",".join(oi[1]), ai[0], ",".join(ai[1])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
