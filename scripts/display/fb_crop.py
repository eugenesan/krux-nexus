import lcd
lcd.init()
w, stride, rows = 1080, 4352, 500
f = open("/dev/graphics/fb0", "rb", buffering=0)
for buf in range(3):
    f.seek(buf * stride * 1920)
    with open("/data/local/tmp/fb%d.ppm" % buf, "wb") as g:
        g.write(b"P6\n%d %d\n255\n" % (w, rows))
        for y in range(rows):
            row = f.read(stride)
            if len(row) < stride:
                break
            out = bytearray()
            for x in range(w):
                out += bytes((row[x*4+2], row[x*4+1], row[x*4]))
            g.write(out)
f.close()
print("done")
