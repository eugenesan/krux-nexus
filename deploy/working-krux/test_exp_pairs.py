import sensor

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.VGA)
sensor.run(1)


def snap(tag):
    img = None
    for _ in range(3):
        try:
            img = sensor.snapshot()
        except Exception as e:
            print(tag, "ERR", e)
            return None
    st = img.get_statistics()
    print(tag, "mean", st.l_mean(), "max", st.l_max())
    return img


snap("initial")
sensor.set_gain(0x01FF)
snap("gain-only 1FF")
sensor.set_exposure_gain(0x09D6, 0x01A5)
snap("pair 09D6/01A5")
sensor.set_exposure_gain(0x04E0, 0x01A5)
snap("pair 04E0/01A5")
sensor.set_exposure_gain(0x0C00, 0x01FF)
snap("pair 0C00/01FF")
sensor.set_exposure(0x0C00)
snap("exp-only 0C00")
sensor.run(0)
