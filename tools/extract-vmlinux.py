#!/usr/bin/env python3
"""
extract-vmlinux.py -- pull a raw vmlinux out of a compressed kernel image.

LineageOS ships zImage / zImage-dtb (arm) or Image.gz / Image (arm64). None of
those are greppable for driver names, because the interesting strings live in
the *decompressed* kernel.

This mirrors the kernel tree's scripts/extract-vmlinux: scan for a known
compression magic at an aligned offset, then decompress from there.

    ./extract-vmlinux.py <boot.img> <out.bin>

Prints the offset and method used. Exits non-zero if nothing decompresses,
which is not fatal -- /proc/config.gz on a live device is exact and better.
"""
import bz2
import gzip
import lzma
import struct
import sys
import zlib

# (name, magic bytes, decompressor)
METHODS = [
    ("gzip",  b"\x1f\x8b\x08", lambda d: gzip.decompress(d)),
    ("xz",    b"\xfd7zXZ\x00", lambda d: lzma.decompress(d, format=lzma.FORMAT_XZ)),
    ("lzma",  b"\x5d\x00\x00", lambda d: lzma.decompress(d, format=lzma.FORMAT_ALONE)),
    ("bzip2", b"BZh",         lambda d: bz2.decompress(d)),
    ("lz4",   b"\x04\x22\x4d\x18", lambda d: _lz4(d)),
    ("zstd",  b"\x28\xb5\x2f\xfd", lambda d: _zstd(d)),
]


def _lz4(d):
    try:
        import lz4.frame
        return lz4.frame.decompress(d)
    except ImportError:
        raise ValueError("lz4 module unavailable")


def _zstd(d):
    try:
        import zstandard
        return zstandard.ZstdDecompressor().decompress(d, max_output_size=1 << 30)
    except ImportError:
        raise ValueError("zstandard module unavailable")


def entropy(blob, sample=262144):
    """Shannon entropy in bits/byte. ~8.0 means compressed or encrypted."""
    import collections
    b = blob[:sample]
    n = len(b)
    if not n:
        return 0.0
    return -sum((c / n) * _log2(c / n) for c in collections.Counter(b).values())


def _log2(x):
    import math
    return math.log2(x)


def try_decompress(blob, name, magic, fn, size):
    off = blob.find(magic)
    while off != -1:
        try:
            out = fn(blob[off:])
            if len(out) > size:          # plausible vmlinux
                return out, off, name
        except Exception:
            pass
        off = blob.find(magic, off + 1)
    return None


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    blob = open(sys.argv[1], "rb").read()
    out_path = sys.argv[2]

    print("input %s (%d bytes)" % (sys.argv[1], len(blob)))
    if blob[:8] == b"ANDROID!":
        f = struct.unpack("<10I", blob[8:48])
        ksize, page = f[0], f[7]
        blob = blob[page:page + ksize]
        print("  ANDROID boot image -> kernel at offset %d, %d bytes" % (page, ksize))
    is_zimage = blob[:4] in (struct.pack("<I", 0x016f2818), struct.pack("<I", 0xe1a00000))
    if is_zimage:
        print("  ARM zImage wrapper detected")

    for name, magic, fn in METHODS:
        r = try_decompress(blob, name, magic, fn, len(blob))
        if r:
            out, off, how = r
            with open(out_path, "wb") as fh:
                fh.write(out)
            print("  %s payload at offset 0x%x -> %s (%d bytes)"
                  % (how, off, out_path, len(out)))
            return

    # Honest failure. Report the evidence rather than guessing.
    ent = entropy(blob)
    print("  no payload decompressed with gzip/xz/lzma/bzip2/lz4/zstd.")
    if is_zimage or ent > 7.5:
        print()
        print("  The kernel region measures %.2f bits/byte, so it IS compressed." % ent)
        print("  Qualcomm's CAF kernels wrap the payload in LZMA with a")
        print("  non-standard header that Python's lzma module will not decode.")
        print()
        print("  Use one of these instead:")
        print("    * BEST  -- pull /proc/config.gz from a running device.")
        print("               It is world-readable on a stock build and is EXACT,")
        print("               which beats string-scraping a decompressed vmlinux:")
        print("                 adb shell cat /proc/config.gz | gzip -d > kconfig")
        print("    * scripts/extract-vmlinux from the kernel tree you are")
        print("      building, run inside that tree:")
        print("        kernel/lge/hammerhead/scripts/extract-vmlinux < boot.img")
        print("    * binwalk, or `file` on the extracted kernel, to identify the")
        print("      compression before trying tools.")
    sys.exit(1)


if __name__ == "__main__":
    main()