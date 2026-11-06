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
    time.sleep(0.05)
    if i % 10 == 0:
        print("dec i=%d snap=%.1fms dec=%.1fms" % (
            i, (t1 - t0) * 1000.0, (t2 - t1) * 1000.0))
print("dec avg snap=%.1fms dec=%.1fms" % (
    ts / N * 1000.0, td / N * 1000.0))
