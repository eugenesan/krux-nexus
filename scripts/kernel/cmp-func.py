#!/usr/bin/env python3
"""
cmp-func.py -- compare one function of the author's kernel with ours.

The author's ROM kernel and ours are built from the same source tree,
but with different compilers, so the *code* is never identical.  What is
identical in meaning -- and what the camera fault is about -- are the
*constants*: the structure offsets the driver reads out of the camera
HAL's ioctl, and the register offsets it writes to the hardware.

The camera2 HAL is a proprietary binary that was built against the
author's kernel.  If his kernel reads the HAL's "channel 0" from a
different offset than ours does, the CSID's channel lookup table is
programmed for the wrong virtual channel -- which is exactly the fault
we are chasing (the image's packets arrive on VC 1, the table maps
VC 0).

So this script disassembles the same function in both kernels and
compares the constants: the offsets, the masks, the register numbers.

Usage:
  cmp-func.py <ours.vmlinux> <author.vmlinux.bin> <author-kallsyms> <func>
"""
import re
import subprocess
import sys

OBJDUMP = "arm-none-eabi-objdump"
TOOLCHAIN = "/home/user/Develop/toolchains/arch-16.2.0/usr/bin/"


def run(cmd):
    return subprocess.run(cmd, shell=True, capture_output=True, text=True).stdout


def kallsyms(path):
    syms = {}
    for line in open(path, errors="replace"):
        parts = line.split()
        if len(parts) == 3 and parts[1] in "tT":
            try:
                syms[parts[2]] = int(parts[0], 16)
            except ValueError:
                pass
    return syms


def author_disasm(binpath, syms, func, text_base):
    """disassemble a function out of the author's raw vmlinux"""
    if func not in syms:
        return None
    start = syms[func]
    # the end: the next symbol
    nxt = [a for _, a in sorted(((n, a) for n, a in syms.items() if a > start),
                                key=lambda x: x[1])]
    end = nxt[0] if nxt else start + 0x400
    out = run("%s%s -D -b binary -m arm --adjust-vma=0x%x "
              "--start-address=0x%x --stop-address=0x%x %s"
              % (TOOLCHAIN, OBJDUMP, text_base, start, end, binpath))
    return out


def our_disasm(elf, func):
    out = run("%s%s -d --disassemble=%s %s" % (TOOLCHAIN, OBJDUMP, func, elf))
    return out


def constants(text):
    """the immediate constants a disassembly mentions"""
    vals = set()
    for m in re.finditer(r"#(0x[0-9a-fA-F]+|\d+)\b", text):
        v = m.group(1)
        try:
            vals.add(int(v, 0))
        except ValueError:
            pass
    return vals


def main():
    if len(sys.argv) < 5:
        print(__doc__)
        return 1
    ours_elf, author_bin, ksym_file, func = sys.argv[1:5]
    syms = kallsyms(ksym_file)
    text_base = syms.get("_text", 0)
    a = author_disasm(author_bin, syms, func, text_base)
    o = our_disasm(ours_elf, func)
    if a is None:
        print("  the author's kernel has no %s" % func)
        return 1
    if not o.strip():
        print("  our kernel has no %s" % func)
        return 1
    ca, co = constants(a), constants(o)
    print("== %s" % func)
    print("   the author's constants: %d, ours: %d" % (len(ca), len(co)))
    only_a = sorted(ca - co)
    only_o = sorted(co - ca)
    print("   only in the author's: %s" % ", ".join(hex(v) for v in only_a))
    print("   only in ours:         %s" % ", ".join(hex(v) for v in only_o))
    return 0


if __name__ == "__main__":
    sys.exit(main())
