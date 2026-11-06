import sensor, gc, sys, time
sensor.reset(); sensor.set_pixformat(sensor.RGB565); sensor.set_framesize(sensor.QVGA); sensor.run(1)
sensor.set_vts(0x0c4b)
sensor.set_exposure_gain(0x0c00, 0x01ff)
for i in range(5):
    img = sensor.snapshot()
    print('frame', i, img.get_statistics().l_mean())
b = img.to_bytes()
with open('/data/krux/frame-test.rgb565','wb') as f: f.write(b)
print('saved', len(b))
sensor.run(0)
