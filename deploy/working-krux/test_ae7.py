import sensor, time
sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.run(1)
time.sleep(1)
# baseline
img = sensor.snapshot()
s = img.get_statistics()
print("BASELINE:", s.l_mean())
# increase exposure
sensor.set_exposure(2000)
time.sleep(2)
img = sensor.snapshot()
s = img.get_statistics()
print("EXPOSED:", s.l_mean())
sensor.run(0)
print("done")
