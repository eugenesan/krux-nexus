import sensor

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.VGA)
sensor.run(1)

means = []
for i in range(10):
    img = sensor.snapshot()
    st = img.get_statistics()
    means.append(st.l_mean())
    print(i, "mean", st.l_mean(), "min", st.l_min(), "max", st.l_max())
print("spread", max(means) - min(means), "avg", sum(means) // len(means))
sensor.run(0)
