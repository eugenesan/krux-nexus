import sensor

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.VGA)
sensor.run(1)


def measure(tag, n=8):
    means = []
    for _ in range(n):
        img = sensor.snapshot()
        means.append(img.get_statistics().l_mean())
    print(tag, means, "spread", max(means) - min(means))


measure("defaults")
sensor.set_exposure_gain(0x04D0, 0x01FF)
measure("exp 04d0 gain 01ff")
sensor.set_vts(0x09D6)
sensor.set_exposure_gain(0x0800, 0x01FF)
measure("vts 09d6 exp 0800")
sensor.set_exposure_gain(0x09C0, 0x01FF)
measure("vts 09d6 exp 09c0")
sensor.run(0)
