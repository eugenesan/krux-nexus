echo "=== test the framebuffer band paint only (skip the USB part) ==="
dd if=/dev/zero bs=4352 count=400 2>/dev/null | tr '\000' '\377' > /tmp/band.raw
ls -l /tmp/band.raw
echo -n "  buffer 0: "
dd if=/tmp/band.raw of=/dev/graphics/fb0 bs=4352 seek=0 conv=notrunc 2>&1 | tail -1
echo -n "  buffer 1: "
dd if=/tmp/band.raw of=/dev/graphics/fb0 bs=4352 seek=1920 conv=notrunc 2>&1 | tail -1
echo
echo "=== does the whole rcS script run cleanly? (dry, USB part may drop adb) ==="
echo "  (skipping to avoid cutting our own connection)"
