import sensor

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.VGA)
sensor.run(1)


def measure(tag, n=4):
    means = []
    for _ in range(n):
        img = sensor.snapshot()
        means.append(img.get_statistics().l_mean())
    print(tag, means)
    return means


measure("default vts 04e4")
sensor.set_vts(0x09D6)
measure("vts 09d6 exp default")
sensor.set_exposure_gain(0x04D0, 0x01FF)
measure("vts 09d6 exp 04d0")
sensor.set_exposure_gain(0x0800, 0x01FF)
measure("vts 09d6 exp 0800")
sensor.set_exposure_gain(0x09C0, 0x01FF)
measure("vts 09d6 exp 09c0")
sensor.set_exposure_gain(0x0800, 0x00FF)
measure("exp 0800 gain 00ff")
sensor.set_exposure_gain(0x0800, 0x0080)
measure("exp 0800 gain 0080")
sensor.run(0)
