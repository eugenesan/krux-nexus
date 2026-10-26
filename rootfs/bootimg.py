#!/usr/bin/env python3
"""
bootimg.py -- unpack and repack an ANDROID! boot image for hammerhead.

There is no mkbootimg on this host and no AOSP tree, and the header is 40 bytes
of fixed fields followed by some strings, so it is written out longhand rather
than chased down a dependency.

The addresses in the header are NOT ours to choose. `fastboot boot` hands the
image to the stock bootloader, which loads the kernel and ramdisk at exactly
the addresses in this header. Getting them wrong produces a device that boots
to nothing at all, with no error message. So unpack records every field, and
pack refuses to invent one -- it reuses what unpack saw.

Layout (header v0, little-endian u32 unless noted):

    0   8s   magic "ANDROID!"
    8   4    kernel_size
    12  4    kernel_addr
    16  4    ramdisk_size
    20  4    ramdisk_addr
    24  4    second_size
    28  4    second_addr
    32  4    tags_addr
    36  4    page_size
    40  4    dt_size            (v1+)
    44  4    unused
    48  16s  name
    64  512s cmdline
    576 32s  id
    608 1024s extra_cmdline     (v2+)

Usage:
    bootimg.py unpack <boot.img> <outdir>
    bootimg.py pack   <outdir> <ramdisk.cpio.gz> <out.boot.img> [--extra-cmdline S]
"""
import hashlib
import struct
import sys
from pathlib import Path

HDR_FMT = "<8sIIIIIIII"
HDR_LEN = 40
OFF_NAME = 48
OFF_CMDLINE = 64
OFF_ID = 576
CMDLINE_LEN = 512


def rd_str(buf, off, n):
    return buf[off:off + n].split(b"\0", 1)[0].decode("utf-8", "replace")


def rd_u32(buf, off):
    return struct.unpack_from("<I", buf, off)[0]


def do_unpack(src, outdir):
    src, outdir = Path(src), Path(outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    data = src.read_bytes()

    if data[:8] != b"ANDROID!":
        sys.exit(f"!! not an ANDROID! boot image (magic={data[:8]!r})")

    (magic, ksize, kaddr, rsize, raddr,
     ssize, saddr, taddr, psize) = struct.unpack_from(HDR_FMT, data, 0)

    print(f"   magic        {magic.decode()}")
    print(f"   kernel       {ksize:>9} bytes  @ 0x{kaddr:08x}")
    print(f"   ramdisk      {rsize:>9} bytes  @ 0x{raddr:08x}")
    print(f"   second       {ssize:>9} bytes  @ 0x{saddr:08x}")
    print(f"   tags         0x{taddr:08x}")
    print(f"   page_size    {psize}")
    print(f"   name         {rd_str(data, OFF_NAME, 16)!r}")
    print(f"   cmdline      {rd_str(data, OFF_CMDLINE, CMDLINE_LEN)!r}")

    if psize == 0 or psize % 2048:
        sys.exit(f"!! page_size {psize} is not a sane power of two")

    def carve(off_pages, size, name):
        """Android packs each section page-aligned."""
        start = off_pages * psize
        blob = data[start:start + size]
        if len(blob) != size:
            sys.exit(f"!! {name} truncated: want {size}, got {len(blob)}")
        (outdir / name).write_bytes(blob)
        return hashlib.sha256(blob).hexdigest()

    kp = 1                                   # kernel follows the header page
    ksha = carve(kp, ksize, "kernel")
    rp = kp + (ksize + psize - 1) // psize   # ramdisk follows the kernel
    rsha = carve(rp, rsize, "ramdisk")
    if ssize:
        sp = rp + (rsize + psize - 1) // psize
        carve(sp, ssize, "second")

    meta = {
        "kernel_addr": kaddr, "ramdisk_addr": raddr, "second_addr": saddr,
        "tags_addr": taddr, "page_size": psize,
        "name": rd_str(data, OFF_NAME, 16),
        "cmdline": rd_str(data, OFF_CMDLINE, CMDLINE_LEN),
        "id": data[OFF_ID:OFF_ID + 32],
        "kernel_sha256": ksha, "ramdisk_sha256": rsha,
        "header_bytes": data[:OFF_CMDLINE],   # keep magic..unused verbatim
    }
    (outdir / "meta.txt").write_text(
        "\n".join(f"{k}={v!r}" for k, v in meta.items()))
    print(f"   kernel  sha256 {ksha[:16]}...")
    print(f"   ramdisk sha256 {rsha[:16]}...")
    print(f"   -> {outdir}/")


def do_pack(indir, ramdisk_path, out_path, extra_cmdline=""):
    indir, outdir_p = Path(indir), Path(ramdisk_path)
    meta = {}
    for line in (indir / "meta.txt").read_text().splitlines():
        k, _, v = line.partition("=")
        meta[k] = eval(v)          # written by us, repr() round-trips exactly

    kernel = (indir / "kernel").read_bytes()
    ramdisk = Path(ramdisk_path).read_bytes()
    psize = meta["page_size"]

    cmdline = meta["cmdline"]
    if extra_cmdline:
        cmdline = (cmdline + " " + extra_cmdline).strip()
    if len(cmdline.encode()) >= CMDLINE_LEN:
        sys.exit("!! cmdline too long for the header field")

    hdr = bytearray(meta["header_bytes"])
    struct.pack_into("<IIII", hdr, 8,
                     len(kernel), meta["kernel_addr"],
                     len(ramdisk), meta["ramdisk_addr"])
    # second/tags left exactly as unpacked
    struct.pack_into("<I", hdr, 36, psize)
    hdr[OFF_NAME:OFF_NAME + 16] = meta["name"].encode().ljust(16, b"\0")[:16]
    hdr[OFF_CMDLINE:OFF_CMDLINE + CMDLINE_LEN] = \
        cmdline.encode().ljust(CMDLINE_LEN, b"\0")[:CMDLINE_LEN]
    hdr[OFF_ID:OFF_ID + 32] = meta["id"]

    def pad(b):
        return b + b"\0" * (-len(b) % psize)

    out = bytes(hdr).ljust(psize, b"\0")
    out += pad(kernel)
    out += pad(ramdisk)
    Path(out_path).write_bytes(out)

    print(f"   wrote {out_path}  ({len(out):,} bytes)")
    print(f"     kernel  {len(kernel):>9,} @ 0x{meta['kernel_addr']:08x}")
    print(f"     ramdisk {len(ramdisk):>9,} @ 0x{meta['ramdisk_addr']:08x}"
          "   <- ours")
    print(f"     cmdline {cmdline!r}")
    if len(out) > 23068672:
        print(f"     NOTE: exceeds the boot partition size (23,068,672).")
        print(f"           `fastboot boot` may still take it if it fits in RAM,")
        print(f"           but `fastboot flash boot` would refuse.")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    if sys.argv[1] == "unpack" and len(sys.argv) == 4:
        do_unpack(sys.argv[2], sys.argv[3])
    elif sys.argv[1] == "pack" and len(sys.argv) >= 5:
        extra = ""
        if "--extra-cmdline" in sys.argv:
            extra = sys.argv[sys.argv.index("--extra-cmdline") + 1]
        do_pack(sys.argv[2], sys.argv[3], sys.argv[4], extra)
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
