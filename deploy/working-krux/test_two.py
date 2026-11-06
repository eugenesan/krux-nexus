import sensor

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.VGA)
sensor.run(1)

sensor.set_exposure_gain(0x0010, 0x00E0)
for _ in range(3):
    img = sensor.snapshot()
with open("/data/krux/exp-low.rgb565", "wb") as f:
    f.write(img.to_bytes())
print("low", img.get_statistics().l_mean())

sensor.set_exposure_gain(0x04D0, 0x01FF)
for _ in range(3):
    img = sensor.snapshot()
with open("/data/krux/exp-high.rgb565", "wb") as f:
    f.write(img.to_bytes())
print("high", img.get_statistics().l_mean())

sensor.run(0)
