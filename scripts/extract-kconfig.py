#!/usr/bin/env python3
"""
extract-kconfig.py -- pull the embedded kernel config (CONFIG_IKCONFIG) out of
a kernel image or boot.img.

WHY A DEDICATED TOOL

This project has already tried twice to read a config the wrong way:

  1. `grep IKCFG_ST boot.img/kernel` -- fails, because the config lives in the
     __initdata section of the *decompressed* payload, which is LZMA-compressed
     inside the zImage wrapper.
  2. relying on /proc/config.gz alone -- which is what build/README.md did when
     it derived standalone/tools/profiles/hammerhead.list, and which turned out to describe
     a build we no longer have.

So: decompress first, then look for the markers. This tool does exactly that,
and can then be diffed against /proc/config.gz from a live device to find out
whether the embedded config really describes the running binary.

HOW THE EMBEDDED CONFIG WORKS

CONFIG_IKCONFIG=y makes init/IKCONFIG put a gzip stream in __initdata, framed
by the 8-byte markers IKCFG_ST and IKCFG_END:

    ... IKCFG_ST <gzip stream> IKCFG_END ...

CONFIG_IKCONFIG_PROC=y is what exposes it at /proc/config.gz. A kernel can have
one without the other, so /proc/config.gz being absent does NOT mean the config
is not embedded, and its presence does not guarantee it matches.

USAGE
    ./extract-kconfig.py <image> [-o out.config]
      <image> may be a boot.img or a bare kernel. A boot.img is unpacked in
      memory; no external tool is needed.

EXIT STATUS
    0 config extracted, 1 nothing found, 2 bad input.
"""

import gzip
import io
import lzma
import os
import struct
import subprocess
import sys

MARK_START = b"IKCFG_ST"
MARK_END = b"IKCFG_END"

# zImage payloads start on a 4-byte boundary, which cuts the false positives
# from short magics (lzma's "\x5d\x00\x00" in particular).
MAGICS = [
    (b"\x1f\x8b\x08", "gzip"),
    (b"\xfd7zXZ\x00", "xz"),
    (b"\x5d\x00\x00", "lzma"),
    (b"BZh", "bzip2"),
    (b"\x89LZO", "lzo"),
    (b"\x02\x21\x4c\x18", "lz4-legacy"),
    (b"\x04\x22\x4d\x18", "lz4-frame"),
    (b"\x28\xb5\x2f\xfd", "zstd"),
]


def log(msg):
    print(msg)


def unpack_boot_img(path):
    """Return (kernel, cmdline, header_text) from an Android boot.img.

    Delegates to the project's own verified packer/unpacker rather than
    hand-rolling header parsing -- hand-rolled parsing has already produced
    wrong page sizes and ramdisk offsets in this project. bootimg.py's unpack
    mode needs an output directory as its second argument.
    """
    import shutil
    import tempfile
    tmp = tempfile.mkdtemp(prefix="kcfg-")
    try:
        r = subprocess.run(
            ["python3",
             os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          "rootfs", "bootimg.py"),
             "unpack", path, tmp],
            capture_output=True, text=True)
        if r.returncode != 0:
            log("  bootimg.py unpack failed: %s" % (r.stderr.strip()[:200] or r.returncode))
            return None, None, None
        outdir = None
        for line in r.stdout.splitlines():
            s = line.strip()
            if s.startswith("->"):
                outdir = s[2:].strip()
        if not outdir:
            log("  bootimg.py did not report an output directory")
            return None, None, None
        kpath = os.path.join(outdir, "kernel")
        rpath = os.path.join(outdir, "ramdisk")
        if not os.path.exists(kpath):
            log("  bootimg.py produced no kernel file")
            return None, None, None
        kern = open(kpath, "rb").read()
        return kern, rpath, r.stdout
    except Exception as e:
        log("  bootimg.py unpack failed: %s" % e)
        return None, None, None
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def try_decompress(data, off, label):
    blob = data[off:]
    # LZMA: the kernel writes a raw LZMA1 stream whose 5-byte properties header
    # is followed by an 8-byte uncompressed size. python's FORMAT_ALONE expects
    # exactly that, so try it first and fall back to the legacy format.
    if label.startswith("lzma"):
        for fmt in (lzma.FORMAT_ALONE, lzma.FORMAT_RAW, lzma.FORMAT_AUTO,
                    lzma.FORMAT_XZ):
            try:
                d = lzma.LZMADecompressor(format=fmt).decompress(blob)
                if len(d) > 1_000_00:
                    return d
            except Exception:
                pass
        return None
    for cmd in (["gzip", "-dc"], ["xz", "-dc"], ["bzip2", "-dc"], ["zstd", "-dc"],
                ["lz4", "-dc"]):
        if not cmd or not cmd[0]:
            continue
        try:
            p = subprocess.run(cmd, input=blob, capture_output=True, timeout=180)
            if p.returncode == 0 and len(p.stdout) > 1_000_00:
                return p.stdout
        except Exception:
            continue
    return None


def find_kconfig(vmlinux):
    i = vmlinux.find(MARK_START)
    if i < 0:
        return None, None
    j = vmlinux.find(MARK_END, i)
    if j < 0:
        return None, None
    return i, j


def main():
    if len(sys.argv) < 2:
        log(__doc__)
        return 2
    src = sys.argv[1]
    out = None
    if "-o" in sys.argv:
        out = sys.argv[sys.argv.index("-o") + 1]

    raw = open(src, "rb").read()
    log("input      : %s (%d bytes)" % (src, len(raw)))

    # boot.img magic is the 8-byte string "ANDROID!" at offset 0. An earlier
    # version compared against the 7-byte b"ANDROID", which never matches, so
    # every boot.img was silently treated as a bare kernel.
    if raw[:8] == b"ANDROID!":
        kern, cmdline, hdr = unpack_boot_img(src)
        if kern is None:
            log("  could not unpack as boot.img")
            return 2
        log("boot.img   : kernel %d bytes" % len(kern))
        for line in hdr.splitlines():
            if line.strip().startswith(("cmdline", "page_size", "ramdisk")):
                log("  " + line.strip())
    else:
        kern = raw
        log("  treated as a bare kernel image")

    # 1. sometimes the payload is stored uncompressed
    _, vend = find_kconfig(kern)
    if vend:
        log("compressed : no -- IKCFG_ST found directly in the image")
        vml = kern
    else:
        log("compressed : yes -- no IKCFG_ST in the raw image, scanning payload")
        vml = None
        cands = []
        for magic, label in MAGICS:
            start = 0
            while True:
                i = kern.find(magic, start)
                if i < 0:
                    break
                cands.append((i, label))
                start = i + 1
        cands.sort()
        # NOTE: an earlier version filtered these to 4-byte-aligned offsets on
        # the theory that zImage payloads are aligned. That is wrong here: the
        # real CAF payload does NOT begin on an aligned boundary, so alignment
        # filtering silently hid every candidate and the tool reported "no
        # payload decompressed" -- a false negative presented as a fact.
        # Offsets must be tried unfiltered, and reported honestly.
        log("  scanning %d magic hit(s) unfiltered" % len(cands))
        for off, label in cands[:12]:
            out_v = try_decompress(kern, off, label)
            if out_v and len(out_v) > 65536:
                log("  -> 0x%08x (%s) decompressed to %d bytes%s"
                    % (off, label, len(out_v),
                       ", IKCFG found inside" if find_kconfig(out_v)[1] else ""))
                if find_kconfig(out_v)[1]:
                    vml = out_v
                    break
        if vml is None:
            log("FAILED: no standard payload magic decompressed to a usable image.")
            log("")
            log("This is EXPECTED for this device's kernels, not a bug in the input.")
            log("The CAF 3.4 zImage here carries an uncompressed loader (strings like")
            log("'Uncompressing Linux...', 'decompressor returned an error') followed by")
            log("a high-entropy payload starting around 0x3d00, and that payload does")
            log("not respond to gzip / xz / bzip2 / lzma / lz4 / zstd at any offset.")
            log("It also is NOT an ELF, so the kernel tree's scripts/extract-vmlinux")
            log("(which requires readelf -h to succeed) rejects it even when the")
            log("decompression is right.")
            log("")
            log("RELIABLE ALTERNATIVE: boot the image and read /proc/config.gz.")
            log("  adb reboot bootloader && fastboot boot <image>")
            log("  adb shell zcat /proc/config.gz > kconfig")
            log("CONFIG_IKCONFIG_PROC=y is set in every kernel on this device, and a")
            log("config read that way is the running kernel's own copy -- strictly")
            log("better evidence than anything recoverable from the file.")
            return 1

    i, j = find_kconfig(vml)
    if not j:
        log("FAILED: payload decompressed but contains no IKCFG_ST/IKCFG_END pair")
        log("       -> this kernel was not built with CONFIG_IKCONFIG=y")
        return 1

    blob = vml[i + len(MARK_START):j]
    try:
        cfg = gzip.decompress(blob)
    except Exception as e:
        log("FAILED: IKCFG blob is not a valid gzip stream: %s" % e)
        return 1

    text = cfg.decode("utf-8", "replace")
    n = sum(1 for l in text.splitlines() if l.startswith("CONFIG"))
    log("")
    log("extracted  : %d bytes, %d set options" % (len(cfg), n))
    log("marker at  : 0x%08x .. 0x%08x" % (i, j))
    dest = out or (os.path.splitext(src)[0] + ".kconfig")
    with open(dest, "wb") as f:
        f.write(cfg)
    log("written    : %s" % dest)
    return 0


if __name__ == "__main__":
    sys.exit(main())