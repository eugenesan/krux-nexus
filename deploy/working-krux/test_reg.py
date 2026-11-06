import sensor, time
sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.run(1)
time.sleep(1)
# Try writing a single register
try:
    sensor.__write_reg(0x0202, 0x09)
    print("write_reg OK")
except Exception as e:
    print("write_reg failed:", e)
sensor.run(0)
print("done")
