import sensor

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.VGA)
sensor.run(1)

sensor.set_exposure_gain(0x04D0, 0x01FF)
for _ in range(2):
    sensor.snapshot()

for i in range(2):
    img = sensor.snapshot()
    st = img.get_statistics()
    print(i, "mean", st.l_mean())
    with open("/data/krux/pair%d.rgb565" % i, "wb") as f:
        f.write(img.to_bytes())
sensor.run(0)
