import lcd
lcd.init()
w, h, stride = 1080, 1920, 4352
f = open("/dev/graphics/fb0", "rb", buffering=0)
for buf in range(3):
    f.seek(buf * stride * h)
    nz = 0
    total = 0
    for i in range(0, h, 8):
        row = f.read(stride * 8)
        for j in range(0, len(row), 8):
            if row[j]:
                nz += 1
            total += 1
    print("buffer %d: %d of %d sampled bytes are not zero" % (buf, nz, total))
f.close()
