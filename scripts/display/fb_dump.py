# dump the framebuffer so that the result can be looked at
import os, sys
FB = "/dev/graphics/fb0"
fd = os.open(FB, os.O_RDWR)
# the geometry, as the display module sees it
import lcd
lcd.init()
w, h = 1080, 1920
stride = 4352
data = os.pread(fd, stride * h, 0)
with open("/data/local/tmp/fb.ppm", "wb") as f:
    f.write(b"P6\n%d %d\n255\n" % (w, h))
    for y in range(h):
        row = data[y*stride : y*stride + w*4]
        out = bytearray()
        for x in range(w):
            b, g, r = row[x*4], row[x*4+1], row[x*4+2]
            out += bytes((r, g, b))
        f.write(out)
print("wrote /data/local/tmp/fb.ppm")
