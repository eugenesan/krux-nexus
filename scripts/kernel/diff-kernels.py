#!/usr/bin/env python3
"""
diff-kernels.py -- compare two kernel symbol tables, function by function.

The author's ROM kernel is built from commits that are not published, so
the only way to see what he changed is to compare his binary with ours.
Both symbol tables are available:

  ours     nm on the ELF vmlinux of our build
  the author   /proc/kallsyms from his own recovery (the addresses are
                real once kptr_restrict is 0)

The two kernels are built with different compilers, so the code is never
byte-identical and the function *addresses* mean nothing.  What does mean
something:

  * a symbol that exists in one and not the other -- a function the
    author added or removed;
  * a function whose *size* is very different -- a function the author
    changed.

Usage: diff-kernels.py <ours.vmlinux> <author-kallsyms.txt> [filter]
"""
import subprocess
import sys
from collections import OrderedDict


def symbols_from_nm(path):
    out = {}
    for line in subprocess.run(["nm", path], capture_output=True,
                               text=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] in "tT":
            out[parts[2]] = int(parts[0], 16)
    return out


def symbols_from_kallsyms(path):
    out = {}
    for line in open(path, errors="replace"):
        parts = line.split()
        if len(parts) == 3 and parts[1] in "tT":
            try:
                out[parts[2]] = int(parts[0], 16)
            except ValueError:
                pass
    return out


def sizes(syms):
    """the size of each symbol: the distance to the next one"""
    ordered = sorted(syms.items(), key=lambda kv: kv[1])
    out = {}
    for i, (name, addr) in enumerate(ordered):
        end = ordered[i + 1][1] if i + 1 < len(ordered) else addr
        out[name] = end - addr
    return out


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    ours = symbols_from_nm(sys.argv[1])
    author = symbols_from_kallsyms(sys.argv[2])
    filt = sys.argv[3] if len(sys.argv) > 3 else ""

    so, sa = sizes(ours), sizes(author)
    only_ours = sorted(set(so) - set(sa))
    only_author = sorted(set(sa) - set(so))

    print("== the symbols: ours %d, the author's %d" % (len(so), len(sa)))
    print()
    print("== only in ours (%d)" % len(only_ours))
    for n in only_ours:
        if filt in n:
            print("   %s" % n)
    print()
    print("== only in the author's (%d)" % len(only_author))
    for n in only_author:
        if filt in n:
            print("   %s" % n)
    print()
    print("== the functions whose size differs the most")
    rows = []
    for n in sorted(set(so) & set(sa)):
        if filt and filt not in n:
            continue
        d = sa[n] - so[n]
        if d:
            rows.append((abs(d), n, so[n], sa[n], d))
    rows.sort(reverse=True)
    for _, n, o, a, d in rows[:40]:
        print("   %-46s ours %5d  the author %5d  (%+d)" % (n, o, a, d))
    return 0


if __name__ == "__main__":
    sys.exit(main())
