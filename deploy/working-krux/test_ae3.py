import sensor, time
sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.run(1)
time.sleep(2)
# Test register write
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
# Now try snapshot
try:
    img = sensor.snapshot()
    s = img.get_statistics()
    print("baseline snapshot OK, Y mean:", s.l_mean())
except Exception as e:
    print("snapshot failed:", e)
sensor.run(0)
print("done")
