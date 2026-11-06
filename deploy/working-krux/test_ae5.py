import sensor, time
sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.run(1)
# Wait for camd to produce first frame
time.sleep(2)
# Test register writes
try:
    sensor.set_exposure(2000)
    print("set_exposure OK")
except Exception as e:
    print("set_exposure failed:", e)
# Try snapshot with retries
for i in range(5):
    try:
        img = sensor.snapshot()
        s = img.get_statistics()
        print("snapshot OK, Y mean:", s.l_mean())
        break
    except Exception as e:
        print("snapshot attempt", i, "failed:", e)
        time.sleep(2)
sensor.run(0)
print("done")
