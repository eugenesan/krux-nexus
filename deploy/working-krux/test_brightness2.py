import sensor, time
sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.run(1)
time.sleep(2)
# Baseline
img = sensor.snapshot()
s = img.get_statistics()
print("baseline:", s.l_mean())
# Increase exposure
sensor.set_exposure(2000)
time.sleep(3)
img = sensor.snapshot()
s = img.get_statistics()
print("exposed:", s.l_mean())
# Increase gain
sensor.set_gain(0x01ff)
time.sleep(3)
img = sensor.snapshot()
s = img.get_statistics()
print("gained:", s.l_mean())
sensor.run(0)
print("done")
