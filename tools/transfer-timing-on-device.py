import sys
sys.path.insert(0, '/data/krux')
import sensor
import time

sensor.run(1)
sensor._connect()


def read_frame():
    sensor._sock.sendall(b"F")
    hdr = sensor._recv_exact(32)
    if hdr[:4] != b"KCF1" or hdr[31] == 1:
        return None
    y = sensor._recv_exact(sensor.WIDTH * sensor.HEIGHT)
    sensor._recv_exact((sensor.WIDTH // 2) * (sensor.HEIGHT // 2) * 2)
    return y


# warm up
y = None
for _ in range(60):
    y = read_frame()
    if y is not None:
        break
    time.sleep_ms(100)
if y is None:
    print("no frame")
    raise SystemExit(1)

# two reads back to back: the daemon returns its latest published frame,
# so the second read is the transfer time with no wait for a new frame.
for label in ("first", "second", "third"):
    t0 = time.ticks_ms()
    read_frame()
    t1 = time.ticks_ms()
    print("%s read: %.1f ms" % (label, t1 - t0))
