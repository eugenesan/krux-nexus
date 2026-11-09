import sys
sys.path.insert(0, '/data/krux')
import sensor
import time

sensor.run(1)
for _ in range(3):
    try:
        sensor.snapshot(3000)
    except Exception:
        pass

t0 = time.ticks_ms()
n = 0
fail = 0
for _ in range(40):
    try:
        img = sensor.snapshot(3000)
        n += 1
    except Exception:
        fail += 1
dt = (time.ticks_ms() - t0) / 1000.0
print("snapshot: %d frames in %.2f s -> %.2f fps (%d failed)"
      % (n, dt, n / dt if dt else 0, fail))
