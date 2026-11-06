import sensor

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.VGA)
sensor.run(1)


def measure(tag):
    means = []
    for _ in range(4):
        img = sensor.snapshot()
        means.append(img.get_statistics().l_mean())
    print(tag, means)


measure("defaults")
sensor.set_exposure_gain(0x0010, 0x00E0)
measure("exp 0010 gain 00e0")
sensor.set_exposure_gain(0x04D0, 0x00E0)
measure("exp 04d0 gain 00e0")
sensor.set_exposure_gain(0x0010, 0x01FF)
measure("exp 0010 gain 01ff")
sensor.run(0)
