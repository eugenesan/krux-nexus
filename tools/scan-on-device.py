import sys
sys.path.insert(0, '/data/krux')
import sensor
import time
from krux.camera import Camera, QR_SCAN_MODE

cam = Camera()
cam.initialize_run(QR_SCAN_MODE)
print("camera up")
# The app does not refocus at page open; it decodes with the focus it
# already has and asks for a sweep only after frames go unread.
print("decoding with the current focus")
# Start from the exposure the sweep found good, so this test exercises
# the lock rather than the acquisition search.
try:
    sensor.set_exposure_gain(0x0300, 0x00a5)
    cam._ae_exposure = 0x0300
    cam._ae_gain = 0x00a5
    print("seeded exposure 0x0300")
except Exception as e:
    print("seed failed", e)
for i in range(120):
    try:
        img = cam.snapshot(2000)
    except Exception as e:
        print("snapshot failed", e)
        continue
    cam.auto_expose_follow(img)
    res = img.find_qrcodes()
    if res:
        print("FOUND after", i, "frames:", res[0].payload())
        print("  exposure=0x%04x gain=0x%04x luma_gain=%d"
              % (cam._ae_exposure, cam._ae_gain, cam._luma_gain))
        cam.lock_exposure()
        try:
            q = res[0]
            cam.set_code_box(q.x(), q.y(), q.w(), q.h())
            print("  locked; code box", q.x(), q.y(), q.w(), q.h())
        except Exception as e:
            print("  code box failed", e)
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
