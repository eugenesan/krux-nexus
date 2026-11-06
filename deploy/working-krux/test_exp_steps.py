import sensor

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.VGA)
sensor.run(1)


def snap(tag):
    try:
        img = sensor.snapshot()
        st = img.get_statistics()
        print(tag, "ok mean", st.l_mean(), "min", st.l_min(), "max", st.l_max())
        return img
    except Exception as e:
        print(tag, "ERR", e)
        return None


snap("initial")
sensor.set_gain(0x01FF)
snap("gain max")
snap("gain max 2")
sensor.set_exposure(0x0C00)
snap("exp 0C00")
snap("exp 0C00 2")
sensor.set_exposure_gain(0x0C00, 0x01FF)
snap("both")
sensor.run(0)
