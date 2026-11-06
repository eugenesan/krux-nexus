import sensor, sys, time
sensor.reset(); sensor.set_pixformat(sensor.RGB565); sensor.set_framesize(sensor.QVGA); sensor.run(1)
for i in range(60):
    img=sensor.snapshot()
    if i % 10 == 0:
        st=img.get_statistics()
        print('frame',i,'mean',st.l_mean(),'max',st.l_max())
        with open('/data/krux/shot%02d.rgb565' % i,'wb') as f: f.write(img.to_bytes())
    r=img.find_qrcodes(True)
    if r:
        print('FOUND',r[0].payload()); break
print('done')
sensor.run(0)
