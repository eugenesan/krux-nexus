import sensor, time
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
time.sleep(2)
# Baseline
img = sensor.snapshot()
s = img.get_statistics()
print("baseline:", s.l_mean())
# Max exposure
sensor.set_exposure(1200)
time.sleep(4)
img = sensor.snapshot()
s = img.get_statistics()
print("max_exposure:", s.l_mean())
# Max gain
sensor.set_gain(0x01ff)
time.sleep(4)
img = sensor.snapshot()
s = img.get_statistics()
print("max_gain:", s.l_mean())
print("done")
