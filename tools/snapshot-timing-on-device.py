import sys
sys.path.insert(0, '/data/krux')
import sensor
import time

sensor.run(1)
sensor._connect()

# warm up until a real frame arrives
y = None
for _ in range(60):
    sensor._sock.sendall(b"F")
    hdr = sensor._recv_exact(32)
    if hdr[:4] != b"KCF1" or hdr[31] == 1:
        time.sleep_ms(100)
        continue
    y = sensor._recv_exact(sensor.WIDTH * sensor.HEIGHT)
    sensor._recv_exact((sensor.WIDTH // 2) * (sensor.HEIGHT // 2) * 2)
    break
if y is None:
    print("no frame")
    raise SystemExit(1)

# time the raw socket read of one frame (luma + chroma)
t0 = time.ticks_ms()
for _ in range(10):
    sensor._sock.sendall(b"F")
    hdr = sensor._recv_exact(32)
    if hdr[:4] != b"KCF1" or hdr[31] == 1:
        continue
    y = sensor._recv_exact(sensor.WIDTH * sensor.HEIGHT)
    sensor._recv_exact((sensor.WIDTH // 2) * (sensor.HEIGHT // 2) * 2)
t1 = time.ticks_ms()
print("raw read: %.1f ms/frame" % ((t1 - t0) / 10.0))

# time the conversion of a frame
import image
t0 = time.ticks_ms()
for _ in range(10):
    img = image.Image.from_nv12(y, sensor.WIDTH, sensor.HEIGHT,
                               sensor.WIDTH, sensor.WIDTH // 2,
                               sensor.WIDTH, sensor.HEIGHT)
t1 = time.ticks_ms()
print("from_nv12: %.1f ms/frame" % ((t1 - t0) / 10.0))
