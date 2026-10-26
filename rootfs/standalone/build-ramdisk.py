#!/usr/bin/env python3
"""build-ramdisk.py -- build the standalone initramfs cpio.

The cpio is the whole userspace: an `init` binary and a few device nodes.
There is no shell, no libc and no /bin.

The device nodes are in the ARCHIVE rather than created at runtime. The
reason is in standalone/tools/devnodes.py: this kernel has no devtmpfs, so a
minimal ramdisk starts with an empty /dev, and mknod(2) returns EINVAL.

That is now only the FALLBACK. The kernel we build ourselves has
CONFIG_DEVTMPFS=y (the camera's char-device major is allocated at runtime and
so cannot be baked in), and our init mounts devtmpfs on /dev itself. If that
mount fails, or succeeds without /dev/graphics/fb0, the init unmounts it and
falls back to the nodes in this archive. So both paths have to work, and the
nodes are still here.

Usage:
    ./build-ramdisk.py [init-binary]        (default: ./init)

Writes:
    ramdisk.cpio
    ramdisk.cpio.gz
"""
import gzip
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEVNODES = os.path.join(HERE, "..", "..", "tools", "devnodes.py")

S_IFCHR = 0o020000
S_IFDIR = 0o040000


def load_devnodes():
    spec = importlib.util.spec_from_file_location("devnodes", DEVNODES)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def main():
    init_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "init")
    if not os.path.isfile(init_path):
        sys.exit("no init binary at " + init_path)

    with open(init_path, "rb") as fh:
        init_bytes = fh.read()

    root = {
        ".": ("dir", S_IFDIR | 0o755),
        "dev": ("dir", S_IFDIR | 0o755),
        "dev/graphics": ("dir", S_IFDIR | 0o755),
        "dev/input": ("dir", S_IFDIR | 0o755),
        "proc": ("dir", S_IFDIR | 0o755),
        "sys": ("dir", S_IFDIR | 0o755),
        # framebuffer: the MSM driver uses class "graphics", so the node is
        # /dev/graphics/fb0 under devtmpfs too -- the path is the same either way.
        "dev/graphics/fb0": ("dev", S_IFCHR | 0o660, 29, 0),
        # touch
        "dev/input/event1": ("dev", S_IFCHR | 0o660, 13, 65),
        # NFC
        "dev/bcm2079x": ("dev", S_IFCHR | 0o660, 10, 65),
        "init": ("file", 0o755, init_bytes),
    }

    devnodes = load_devnodes()
    out = os.path.join(HERE, "ramdisk.cpio")
    n = devnodes.build(root, out)
    print("  ramdisk.cpio       %d bytes" % n)

    gz = out + ".gz"
    with open(out, "rb") as fh:
        data = fh.read()
    # mtime=0 so the archive is reproducible
    with open(gz, "wb") as fh:
        gzfh = gzip.GzipFile(filename="ramdisk.cpio", mode="wb",
                             fileobj=fh, mtime=0)
        gzfh.write(data)
        gzfh.close()
    print("  ramdisk.cpio.gz    %d bytes" % os.path.getsize(gz))


if __name__ == "__main__":
    main()
