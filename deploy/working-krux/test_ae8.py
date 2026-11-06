import sensor, time
sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.run(1)
time.sleep(1)
# Test register writes
try:
    sensor.set_exposure(2000)
    print("set_exposure OK")
except Exception as e:
    print("set_exposure failed:", e)
try:
    sensor.set_gain(0x0180)
    print("set_gain OK")
except Exception as e:
    print("set_gain failed:", e)
# Try snapshot once
try:
    img = sensor.snapshot()
    s = img.get_statistics()
    print("snapshot OK, Y mean:", s.l_mean())
except Exception as e:
    print("snapshot failed:", str(e)[:50])
sensor.run(0)
print("done")
