import sensor, time
sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.run(1)
time.sleep(8)
# Test register writes
try:
    sensor.set_exposure(2000)
    print("set_exposure OK")
except Exception as e:
    print("set_exposure failed:", e)
# Try snapshot with retries
for i in range(10):
    try:
        img = sensor.snapshot()
        s = img.get_statistics()
        print("snapshot OK, Y mean:", s.l_mean())
        break
    except Exception as e:
        print("snapshot attempt", i, "failed:", str(e)[:30])
        time.sleep(1)
sensor.run(0)
print("done")
