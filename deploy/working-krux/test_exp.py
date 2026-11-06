import sensor, gc, sys, time
try:
    sensor.reset(); sensor.set_pixformat(sensor.RGB565); sensor.set_framesize(sensor.QVGA); sensor.run(1)
    sensor.set_vts(0x0c4b)
    for exp, gain in ((0x09d6,0x00a5),(0x09d6,0x01ff),(0x0b00,0x01ff),(0x0c00,0x01ff),(0x0c40,0x01ff)):
        try:
            sensor.set_exposure_gain(exp, gain)
            time.sleep_ms(150)
            img = sensor.snapshot()
            st = img.get_statistics()
            print("exp=%04x gain=%04x mean=%d stdev=%d min=%d max=%d lo=%d hi=%d" % (exp,gain,st.l_mean(),st.l_stdev(),st.l_min(),st.l_max(),st.clipped_low(),st.clipped_high()))
        except Exception as e:
            print("exp=%04x gain=%04x ERR" % (exp,gain)); sys.print_exception(e)
    sensor.run(0)
except Exception as e:
    sys.print_exception(e)
