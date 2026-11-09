"""Measure the camera's real frame rate, on the device.

Requests a dump in a tight loop and watches the dump file's mtime, so
the rate is the daemon's, not adb's.  Run on the phone:

  /data/krux/micropython -X heapsize=32M /tmp/krux-fps.py [seconds]
"""

import os
import socket
import struct
import sys
import time

CTL = "/data/krux/camd-ctl.sock"
DUMP = "/data/krux/scan_full.raw"


def addr_for(path):
    return bytes([1, 0] + list(path.encode()) + [0] * (108 - len(path)))


def ctl(cmd):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(addr_for(CTL))
    s.settimeout(5)
    s.sendall(cmd)
    try:
        r = s.recv(2)
    except OSError:
        r = b""
    s.close()
    return r


def main():
    secs = float(sys.argv[1]) if len(sys.argv) > 1 else 3.0
    # MicroPython's os.stat returns a tuple; the dump is written to a
    # .tmp file and renamed, so the inode changes on every dump.
    try:
        last = os.stat(DUMP)[1]
    except OSError:
        last = 0
    t0 = time.time()
    n = 0
    while time.time() - t0 < secs:
        ctl(b"D")
        deadline = time.time() + 0.5
        while time.time() < deadline:
            try:
                m = os.stat(DUMP)[1]
            except OSError:
                m = last
            if m != last:
                last = m
                n += 1
                break
            time.sleep(0.0005)
    dt = time.time() - t0
    print("%d dumps in %.2f s -> %.2f fps" % (n, dt, n / dt))


if __name__ == "__main__":
    main()
