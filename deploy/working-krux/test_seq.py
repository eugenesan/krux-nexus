import sensor

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.VGA)
sensor.run(1)

for i in range(6):
    img = sensor.snapshot()
    with open("/data/krux/seq%d.rgb565" % i, "wb") as f:
        f.write(img.to_bytes())
    st = img.get_statistics()
    print(i, "mean", st.l_mean(), "min", st.l_min(), "max", st.l_max())
sensor.run(0)
