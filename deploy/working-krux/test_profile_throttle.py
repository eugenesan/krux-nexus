import sensor, time

try:
    import lcd
    HAVE_LCD = True
except Exception:
    HAVE_LCD = False

sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
time.sleep(1.0)

N = 40
ts = td = tr = 0.0
for i in range(N):
    t0 = time.time()
    img = sensor.snapshot()
    t1 = time.time()
    qrs = img.find_qrcodes()
    t2 = time.time()
    if HAVE_LCD and (i % 3 == 0):
        lcd.display(img, oft=(0, 0), roi=(8, 0, 304, 240))
    t3 = time.time()
    ts += t1 - t0
    td += t2 - t1
    tr += t3 - t2
    # Approximate a 20 fps client: do not spin on duplicate frames.
    time.sleep(0.05)
    if i % 10 == 0:
        print("i=%d snap=%.1fms dec=%.1fms rend=%.1fms" % (
            i, (t1 - t0) * 1000.0, (t2 - t1) * 1000.0,
            (t3 - t2) * 1000.0))
print("avg snap=%.1fms dec=%.1fms rend=%.1fms loop=%.1fms fps=%.1f lcd=%d" % (
    ts / N * 1000.0, td / N * 1000.0, tr / N * 1000.0,
    (ts + td + tr) / N * 1000.0, N / (ts + td + tr), HAVE_LCD))
