import sensor, time
sensor.reset(); sensor.set_pixformat(sensor.RGB565); sensor.set_framesize(sensor.QVGA); sensor.run(1)
for i in range(200):
    img=sensor.snapshot()
    if i % 50 == 0: print(i, img.get_statistics().l_mean())
print('done')
sensor.run(0)
