import sensor

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.VGA)
sensor.run(1)


def measure(tag):
    img = None
    for _ in range(3):
        img = sensor.snapshot()
    st = img.get_statistics()
    print(tag, "mean", st.l_mean(), "max", st.l_max())


sensor.set_exposure_gain(0x0020, 0x00E0)
measure("shr 0020 gain 00e0")
sensor.set_exposure_gain(0x0010, 0x00E0)
measure("shr 0010 gain 00e0")
sensor.set_exposure_gain(0x0080, 0x00E0)
measure("shr 0080 gain 00e0")
sensor.set_exposure_gain(0x0480, 0x00E0)
measure("shr 0480 gain 00e0")
sensor.set_exposure_gain(0x0020, 0x01FF)
measure("shr 0020 gain 01ff")
sensor.run(0)
