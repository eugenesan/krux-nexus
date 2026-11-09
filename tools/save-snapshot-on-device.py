import sys
sys.path.append('/data/krux')
import sensor
import time

# Ask for frames exactly as sensor.snapshot() does, but keep the raw
# luma the image module hands to find_qrcodes.
sensor.reset()
for i in range(6):
    try:
        sensor._sock.sendall(b"F")
        hdr = sensor._recv_exact(32)
        if hdr[:4] != b"KCF1" or hdr[31] == 1:
            time.sleep_ms(50)
            continue
        y = sensor._recv_exact(sensor.WIDTH * sensor.HEIGHT)
        sensor._recv_exact((sensor.WIDTH // 2) * (sensor.HEIGHT // 2) * 2)
        with open("/data/krux/snap.raw", "wb") as f:
            f.write(y)
        print("saved frame", i, len(y))
        break
    except Exception as e:
        print("frame failed:", e)
        time.sleep_ms(100)
