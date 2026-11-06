import sensor, time, sys
sensor.reset(); sensor.set_pixformat(sensor.RGB565); sensor.set_framesize(sensor.QVGA); sensor.run(1)
for i in range(80):
    t=time.ticks_ms()
    try:
        img=sensor.snapshot()
        r=img.find_qrcodes(True)
        print(i, 'dt', time.ticks_diff(time.ticks_ms(),t), 'mean', img.get_statistics().l_mean(), 'qr',len(r))
    except Exception as e:
        print(i,'ERR',e); break
sensor.run(0)
