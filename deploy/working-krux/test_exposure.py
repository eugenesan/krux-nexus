import sensor

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.VGA)
sensor.run(1)

for gain in (0x00A5, 0x0100, 0x0180, 0x01FF):
    for exp in (0x0200, 0x0600, 0x0A00, 0x0C00):
        try:
            sensor.set_exposure_gain(exp, gain)
        except Exception as e:
            print("set failed", e)
            continue
        img = None
        try:
            for _ in range(3):
                img = sensor.snapshot()
        except Exception as e:
            print("snap failed gain 0x%04x exp 0x%04x: %s" % (gain, exp, e))
            break
        st = img.get_statistics()
        print("gain 0x%04x exp 0x%04x mean %d min %d max %d"
              % (gain, exp, st.l_mean(), st.l_min(), st.l_max()))

sensor.set_exposure_gain(0x0C00, 0x01FF)
img = sensor.snapshot()
with open("/data/krux/test-frame-max.rgb565", "wb") as f:
    f.write(img.to_bytes())
print("saved")
sensor.run(0)
