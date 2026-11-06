import sensor
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.VGA)
sensor.run(1)

start = time.ticks_ms()
for i in range(200):
    try:
        img = sensor.snapshot()
    except Exception as e:
        print("frame", i, "ERR", e)
        break
    if i % 20 == 0:
        st = img.get_statistics()
        print("frame", i, "mean", st.l_mean(), "max", st.l_max(),
              "elapsed", time.ticks_diff(time.ticks_ms(), start))
else:
    print("completed 200 frames")
sensor.run(0)
