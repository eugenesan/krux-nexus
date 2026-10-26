#!/usr/bin/env python3
"""Build a newc cpio archive containing STATIC DEVICE NODES.

Why: this kernel has no devtmpfs (confirmed: /proc/filesystems lists only
`nodev tmpfs` and `ext4`). A minimal ramdisk therefore starts with an EMPTY
/dev, and mknod(2) returns EINVAL to an unlabelled process (SELinux/devcgroup),
so runtime mknod is not an option either.

The fix is to put the device nodes in the ARCHIVE. The newc format encodes
rdevmajor/rdevminor in the header, so a reader (the kernel's initramfs unpacker)
creates real device nodes with no syscall from userspace.

Layout is the newc magic "070701", 110-byte ASCII header, 4-byte-aligned body,
trailer "TRAILER!!!".
"""
import struct, os, sys

def newc_header(ino, mode, uid, gid, nlink, mtime, filesize, devmajor, devminor,
                rdevmajor, rdevminor, namesize, check=0):
    hdr = b"070701"
    for v in (ino, mode, uid, gid, nlink, mtime, filesize, devmajor, devminor,
              rdevmajor, rdevminor, namesize, check):
        hdr += b"%08X" % (v & 0xFFFFFFFF)
    assert len(hdr) == 110, len(hdr)
    return hdr

def build(root, out, extra_files=()):
    """root: dict of path -> ('dir',mode) | ('file',mode,bytes) | ('dev',mode,maj,min)"""
    buf = bytearray()
    ino = 1
    # directory entries first, sorted so parents precede children
    for path in sorted(root):
        kind = root[path]
        name = path.lstrip("/")
        namesize = len(name) + 1
        pad = (-(110 + namesize)) % 4
        if kind[0] == "dir":
            hdr = newc_header(ino, kind[1], 0, 0, 2, 0, 0, 0, 0, 0, 0, namesize)
            buf += hdr + name.encode() + b"\0" + b"\0" * pad
        elif kind[0] == "file":
            data = kind[2]
            hdr = newc_header(ino, kind[1], 0, 0, 1, 0, len(data), 0, 0, 0, 0, namesize)
            buf += hdr + name.encode() + b"\0" + b"\0" * pad
            buf += data
            buf += b"\0" * ((-len(data)) % 4)
        elif kind[0] == "dev":
            _, mode, maj, minr = kind
            # S_IFCHR = 0x2000, S_IFBLK = 0x6000 ; mode already carries the type
            hdr = newc_header(ino, mode, 0, 0, 1, 0, 0, 0, 0, maj, minr, namesize)
            buf += hdr + name.encode() + b"\0" + b"\0" * pad
        ino += 1
    # trailer
    namesize = len("TRAILER!!!") + 1
    pad = (-(110 + namesize)) % 4
    buf += newc_header(0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, namesize)
    buf += b"TRAILER!!!" + b"\0" + b"\0" * pad
    with open(out, "wb") as f:
        f.write(buf)
    return len(buf)
