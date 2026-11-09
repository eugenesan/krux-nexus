import sys
sys.path.insert(0, '/data/krux')
import sensor
import time
from krux.camera import Camera, QR_SCAN_MODE

cam = Camera()
cam.initialize_run(QR_SCAN_MODE)
print("camera up")
sensor.refocus(True)
print("full sweep requested")
for i in range(120):
    try:
        img = cam.snapshot(2000)
    except Exception as e:
        print("snapshot failed", e)
        continue
    res = img.find_qrcodes()
    if res:
        print("FOUND after", i, "frames:", res[0].payload())
        for _ in range(5):
            sensor._sock.sendall(b"F")
            hdr = sensor._recv_exact(32)
            if hdr[:4] != b"KCF1" or hdr[31] == 1:
                time.sleep_ms(30)
                continue
            y = sensor._recv_exact(sensor.WIDTH * sensor.HEIGHT)
            sensor._recv_exact((sensor.WIDTH // 2) * (sensor.HEIGHT // 2) * 2)
            with open("/data/krux/snap.raw", "wb") as f:
                f.write(y)
            print("saved the next frame luma")
            break
        break
    if i % 5 == 0:
        print("frame", i, "sweeping", sensor.sweeping())
