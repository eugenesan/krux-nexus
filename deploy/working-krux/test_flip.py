import sensor, sys, time
sensor.reset(); sensor.set_pixformat(sensor.RGB565); sensor.set_framesize(sensor.QVGA); sensor.run(1)
for v in (0,1,2,3):
    sensor.__write_reg(0x0101, v)
    time.sleep_ms(200)
    img=sensor.snapshot()
    with open('/data/krux/flip%d.rgb565' % v,'wb') as f: f.write(img.to_bytes())
    print('saved',v,img.get_statistics().l_mean())
sensor.run(0)
