import sensor, sys, time
sensor.reset(); sensor.set_pixformat(sensor.RGB565); sensor.set_framesize(sensor.QVGA); sensor.run(1)
try:
    sensor.set_vts(0x0c4b)
    sensor.set_exposure_gain(0x0c00, 0x01ff)
except Exception as e:
    print('exp err'); sys.print_exception(e)
for i in range(120):
    img=sensor.snapshot()
    if i % 10 == 0:
        st=img.get_statistics(); print('frame',i,'mean',st.l_mean(),'max',st.l_max())
        with open('/data/krux/qrscan%03d.rgb565'%i,'wb') as f: f.write(img.to_bytes())
    r=img.find_qrcodes(True)
    if r:
        print('FOUND',r[0].payload()); break
print('done')
sensor.run(0)
