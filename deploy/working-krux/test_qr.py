import sensor, sys, time
sensor.reset(); sensor.set_pixformat(sensor.RGB565); sensor.set_framesize(sensor.QVGA); sensor.run(1)
found=0
for i in range(300):
    img=sensor.snapshot()
    r=img.find_qrcodes(True)
    if i % 10 == 0:
        st=img.get_statistics()
        print('frame',i,'mean',st.l_mean(),'max',st.l_max(),'qr',len(r))
    if r:
        print('FOUND',r[0].payload())
        found=1
        break
print('done found',found)
sensor.run(0)
