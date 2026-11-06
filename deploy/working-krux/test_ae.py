import sensor
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.VGA)
sensor.run(1)

TARGET = 0x70
exposure = 0x04E0
gain = 0x00E0
sensor.set_exposure_gain(exposure, gain)
# let the sensor settle after the write
for _ in range(3):
    sensor.snapshot()

mean = 0
for step in range(14):
    img = sensor.snapshot()
    st = img.get_statistics()
    mean = st.l_mean()
    print("step", step, "mean", mean, "max", st.l_max(),
          "exp 0x%04x gain 0x%04x" % (exposure, gain))
    if abs(mean - TARGET) <= 5 and st.l_max() < 250:
        break
    if mean < TARGET:
        if gain < 0x01FF:
            gain = min(0x01FF, gain + 0x20)
        elif exposure < 0x0C00:
            exposure = min(0x0C00, exposure + 0x100)
        else:
            break
    else:
        if exposure > 0x0100:
            exposure = max(0x0100, exposure - 0x100)
        elif gain > 0x0040:
            gain = max(0x0040, gain - 0x20)
        else:
            break
    sensor.set_exposure_gain(exposure, gain)
    # one settling frame after every write
    sensor.snapshot()

img = sensor.snapshot()
st = img.get_statistics()
print("final mean", st.l_mean(), "max", st.l_max())
with open("/data/krux/test-frame-ae.rgb565", "wb") as f:
    f.write(img.to_bytes())
print("saved")
sensor.run(0)
