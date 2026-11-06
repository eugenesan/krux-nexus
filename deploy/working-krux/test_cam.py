import sensor, image, gc, sys, time
try:
    print("free before", gc.mem_free())
    sensor.reset()
    sensor.set_pixformat(sensor.RGB565)
    sensor.set_framesize(sensor.QVGA)
    sensor.run(1)
    img = sensor.snapshot()
    print("w,h", img.width(), img.height())
    print("free after", gc.mem_free())
    b = img.to_bytes()
    print("len", len(b), "head", b[:16])
    st = img.get_statistics()
    print("l_mean", st.l_mean(), "stdev", st.l_stdev(), "min", st.l_min(), "max", st.l_max(), "cliplo", st.clipped_low(), "cliphi", st.clipped_high())
    with open("/data/krux/frame.rgb565", "wb") as f:
        f.write(b)
    print("qr", len(img.find_qrcodes(True)))
    sensor.run(0)
    print("done")
except Exception as e:
    sys.print_exception(e)
