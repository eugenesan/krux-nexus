#!/usr/bin/env python3
"""
sdat2img.py — convert an Android OTA data file + transfer.list into a
filesystem image. (Minimal reimplementation of the well-known sdat2img
toolchain, so this repo has no external build dependencies.)

    sdat2img.py <transfer.list> <data file(s)> <output image>
"""
import re
import sys


def parse_transfer_list(path):
    ops = []
    with open(path) as fh:
        fh.readline()                      # version
        fh.readline()                      # total blocks
        max_size = int(fh.readline())      # stash entries
        max_stash = int(fh.readline())     # remove entries
        for line in fh:
            line = line.strip()
            if not line:
                continue
            cmd, _, rest = line.partition(" ")
            ops.append((cmd, [int(x) for x in rest.split(",")]))
    return ops


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    tlist, out = sys.argv[1], sys.argv[3]
    data_files = sys.argv[2].split(",") if "," in sys.argv[2] else sys.argv[2:]

    with open(out, "wb") as outf:
        stash = []          # stack of (len, entry)
        stash_idx = 0
        pos = 0
        for cmd, args in parse_transfer_list(tlist):
            if cmd == "erase":
                outf.truncate(args[2] * 4096)
                pos = args[2]
            elif cmd == "new":
                # new <len>[, <file_id>]
                # With a file id, read from the start of that data file.
                # Without one, continue sequentially from the current position.
                if len(args) > 1:
                    f = open(data_files[args[1]], "rb")
                else:
                    f = open(data_files[0], "rb")
                    f.seek(pos * 4096)
                for _ in range(args[0]):
                    b = f.read(4096)
                    if not b:
                        break
                    outf.write(b)
                    pos += 1
                f.close()
            elif cmd == "stash":
                stash.append([args[0], args[1:]])
                stash_idx = 0
            elif cmd == "free":
                pass
            elif cmd == "move":
                continue
            elif cmd in ("zero",):
                outf.seek(args[-1] * 4096)
                pos = args[-1]
            elif cmd == "cmd":
                continue
            else:
                sys.exit("unhandled transfer.list command: %r" % cmd)

    print("wrote %s" % out)


if __name__ == "__main__":
    main()