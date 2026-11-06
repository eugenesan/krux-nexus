import sensor, time

sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
time.sleep(1.0)

N = 40
ts = td = 0.0
for i in range(N):
    t0 = time.time()
    img = sensor.snapshot()
    t1 = time.time()
    qrs = img.find_qrcodes()
    t2 = time.time()
    ts += t1 - t0
    td += t2 - t1
    if i % 10 == 0:
        print("i=%d snap=%.1fms dec=%.1fms" % (
            i, (t1 - t0) * 1000.0, (t2 - t1) * 1000.0))
print("avg snap=%.1fms dec=%.1fms loop=%.1fms fps=%.1f" % (
    ts / N * 1000.0, td / N * 1000.0,
    (ts + td) / N * 1000.0, N / (ts + td)))
