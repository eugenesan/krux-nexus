import struct, sys

# Android boot image header v0
FMT = "<8sIIIIIIII"   # magic, kernel_size, kernel_addr, ramdisk_size,
                      # ramdisk_addr, second_size, second_addr, tags_addr, page_size

for name in ("boot", "recovery"):
    path = "hdr-%s.bin" % name
    print("=== %s ===" % name)
    with open(path, "rb") as f:
        h = f.read(64)
    magic, ksize, kaddr, rsize, raddr, ssize, saddr, taddr, psize = struct.unpack(FMT, h[:40])
    print("   magic      : %s" % magic.decode("ascii", "replace"))
    print("   kernel_size: %d bytes (%.2f MB)" % (ksize, ksize / 1048576))
    print("   kernel_addr: 0x%08x" % kaddr)
    print("   ramdisk    : %d bytes" % rsize)
    print("   second     : %d bytes" % ssize)
    print("   page_size  : %d" % psize)
    print("   kernel at  : offset %d (page after header)" % psize)
    print()
