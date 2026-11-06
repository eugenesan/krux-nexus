import sensor
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.VGA)
sensor.run(1)

last = None
ok = 0
for i in range(25):
    t = time.ticks_ms()
    try:
        img = sensor.snapshot()
    except Exception as e:
        print(i, "ERR", e)
        break
    dt = time.ticks_diff(time.ticks_ms(), t)
    st = img.get_statistics()
    qr = img.find_qrcodes(True)
    print(i, "dt", dt, "mean", st.l_mean(), "min", st.l_min(),
          "max", st.l_max(), "qr", len(qr))
    ok += 1
    last = img

print("frames", ok)
if last is not None:
    try:
        with open("/data/krux/test-frame.rgb565", "wb") as f:
            f.write(last.to_bytes())
        print("saved frame")
    except Exception as e:
        print("save failed", e)
sensor.run(0)
