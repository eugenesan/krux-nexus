#!/usr/bin/env python3
"""Apply/revert auto-exposure patches to camd.c and sensor.py.

Usage:
    python3 tools/ae-patch.py --apply
    python3 tools/ae-patch.py --revert
"""

import argparse
import os
import sys

PATCHES = {
    "cam/camd.c": [
        # Add W (register write), E (exposure+gain) and V (VTS) commands
        # after the 'F' handler, inside the per-connection read loop.
        (
            "\t\t\t}\n"
            "\t\t}\n"
            "\t\tclose(cfd);\n",
            "\t\t\t}\n"
            "\t\t\t/* AE: write single register */\n"
            "\t\t\tif (buf[0] == 'W') {\n"
            "\t\t\t\tunsigned char reg_hi, reg_lo, val;\n"
            "\t\t\t\tif (read(cfd, &reg_hi, 1) == 1 &&\n"
            "\t\t\t\t    read(cfd, &reg_lo, 1) == 1 &&\n"
            "\t\t\t\t    read(cfd, &val, 1) == 1) {\n"
            "\t\t\t\t\tstruct imx179_reg r = { (reg_hi << 8) | reg_lo, val, 0 };\n"
            "\t\t\t\t\tsensor_write_regs(&r, 1);\n"
            "\t\t\t\t\twrite(cfd, \"OK\", 2);\n"
            "\t\t\t\t}\n"
            "\t\t\t}\n"
            "\t\t\t/* AE: set exposure + gain in one transaction */\n"
            "\t\t\tif (buf[0] == 'E') {\n"
            "\t\t\t\tunsigned char e[4];\n"
            "\t\t\t\tif (read(cfd, e, 4) == 4) {\n"
            "\t\t\t\t\tstruct imx179_reg regs[4] = {\n"
            "\t\t\t\t\t\t{ 0x0202, e[0], 0 },\n"
            "\t\t\t\t\t\t{ 0x0203, e[1], 0 },\n"
            "\t\t\t\t\t\t{ 0x0204, e[2], 0 },\n"
            "\t\t\t\t\t\t{ 0x0205, e[3], 0 },\n"
            "\t\t\t\t\t};\n"
            "\t\t\t\t\tsensor_write_regs(regs, 4);\n"
            "\t\t\t\t\twrite(cfd, \"OK\", 2);\n"
            "\t\t\t\t}\n"
            "\t\t\t}\n"
            "\t\t\t/* AE: set VTS (frame length) for longer exposure */\n"
            "\t\t\tif (buf[0] == 'V') {\n"
            "\t\t\t\tunsigned char v[2];\n"
            "\t\t\t\tif (read(cfd, v, 2) == 2) {\n"
            "\t\t\t\t\tstruct imx179_reg regs[2] = {\n"
            "\t\t\t\t\t\t{ 0x0340, v[0], 0 },\n"
            "\t\t\t\t\t\t{ 0x0341, v[1], 0 },\n"
            "\t\t\t\t\t};\n"
            "\t\t\t\t\tsensor_write_regs(regs, 2);\n"
            "\t\t\t\t\twrite(cfd, \"OK\", 2);\n"
            "\t\t\t\t}\n"
            "\t\t\t}\n"
            "\t\t}\n"
            "\t\tclose(cfd);\n",
        ),
    ],
}

def apply_patch(filepath, old, new):
    with open(filepath, 'r') as f:
        content = f.read()
    if old not in content:
        print(f"  WARNING: pattern not found in {filepath}")
        return False
    content = content.replace(old, new, 1)
    with open(filepath, 'w') as f:
        f.write(content)
    return True

def revert_patch(filepath, old, new):
    with open(filepath, 'r') as f:
        content = f.read()
    if new not in content:
        print(f"  WARNING: patched pattern not found in {filepath}")
        return False
    content = content.replace(new, old, 1)
    with open(filepath, 'w') as f:
        f.write(content)
    return True

def main():
    parser = argparse.ArgumentParser(description="Apply/revert auto-exposure patches")
    parser.add_argument("--apply", action="store_true", help="Apply patches")
    parser.add_argument("--revert", action="store_true", help="Revert patches")
    args = parser.parse_args()

    if not args.apply and not args.revert:
        print("Specify --apply or --revert")
        sys.exit(1)

    for filepath, replacements in PATCHES.items():
        if not os.path.isfile(filepath):
            print(f"Error: {filepath} not found")
            sys.exit(1)
        print(f"Processing {filepath}...")
        for old, new in replacements:
            if args.apply:
                if apply_patch(filepath, old, new):
                    print(f"  Applied patch")
                else:
                    print(f"  FAILED to apply patch")
            else:
                if revert_patch(filepath, old, new):
                    print(f"  Reverted patch")
                else:
                    print(f"  FAILED to revert patch")

if __name__ == "__main__":
    main()