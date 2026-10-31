#!/usr/bin/env python3
"""Summarize a vendor camera capture directory.

Usage: tools/cam-capture-summary.py <capture-dir>

Reads sensor_cfg_<n>_n<count>.bin ({u16 addr, u16 data, u32 delay}) and
isp_cfg_<n>_n<blocks>_l<len>.bin, and reports:

  - the sensor geometry (W/H/VTS/HTS) seen in each sensor array, so a
    binned 1640x1232 session can be told apart from 3280x2464;
  - the ISP upload sequence and the repeating per-frame cycle period;
  - how many ISP uploads belong to the initial configuration.

This is read-only; it does not generate headers.
"""
import glob
import hashlib
import os
import re
import struct
import sys


def sensor_geometry(path):
    regs = {}
    raw = open(path, "rb").read()
    for i in range(len(raw) // 8):
        addr, data, _delay = struct.unpack_from("<HHI", raw, i * 8)
        regs[addr] = data
    def pair(hi, lo):
        if hi in regs and lo in regs:
            return (regs[hi] << 8) | regs[lo]
        return None
    return {
        "W": pair(0x034c, 0x034d),
        "H": pair(0x034e, 0x034f),
        "VTS": pair(0x0340, 0x0341),
        "HTS": pair(0x0342, 0x0343),
    }


def isp_files(d):
    out = []
    for p in glob.glob(os.path.join(d, "isp_cfg_*.bin")):
        m = re.search(r"isp_cfg_(\d+)_n(\d+)_l(\d+)\.bin$",
                      os.path.basename(p))
        if not m:
            continue
        out.append((int(m.group(1)), int(m.group(2)), int(m.group(3)), p))
    out.sort()
    return out


def digest(path):
    return hashlib.md5(open(path, "rb").read()).hexdigest()[:10]


def cycle_period(files):
    if len(files) < 12:
        return None
    # Look for the smallest period in the last 60 uploads.
    tail = files[-60:]
    keys = [(n, l, digest(p)) for _, n, l, p in tail]
    for period in range(1, min(20, len(keys) // 2) + 1):
        ok = True
        for i in range(len(keys) - period):
            if keys[i] != keys[i + period]:
                ok = False
                break
        if ok:
            return period
    return None


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    d = sys.argv[1]
    print("=== sensor geometry")
    seen = {}
    for p in sorted(glob.glob(os.path.join(d, "sensor_cfg_*.bin")),
                    key=lambda x: int(re.search(r"_(\d+)_",
                                                os.path.basename(x)).group(1))):
        g = sensor_geometry(p)
        key = (g["W"], g["H"], g["VTS"], g["HTS"])
        seen.setdefault(key, []).append(os.path.basename(p))
    for key, names in seen.items():
        w, h, vts, hts = key
        tag = "BINNED" if (w, h) == (1640, 1232) else (
            "FULL" if (w, h) == (3280, 2464) else "other")
        print(f"  W={w} H={h} VTS={vts} HTS={hts}  [{tag}]  "
              f"{len(names)} array(s): {', '.join(names[:4])}"
              + (" ..." if len(names) > 4 else ""))

    files = isp_files(d)
    print(f"\n=== ISP uploads: {len(files)}")
    if files:
        period = cycle_period(files)
        print(f"  first index={files[0][0]} last index={files[-1][0]}")
        print(f"  repeating cycle period: {period}")
        print("  first 24 uploads:")
        for idx, n, l, p in files[:24]:
            print(f"    {idx:5d} n={n} l={l} {digest(p)}")
    cpp = len(glob.glob(os.path.join(d, "cpp_cfg_*.bin")))
    print(f"\n=== CPP uploads: {cpp}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
