"""Set the sensor exposure and ask camd for a dump, on the device.

Run by the host script (tools/capture-corpus) on the device.  It
drives camd's control socket and nothing else: the daemon writes the
dump to /data/krux/, and the host pulls the files back (they are
root-only, and the source frame is two megabytes against
MicroPython's one-megabyte default heap).

  /data/krux/micropython /tmp/krux-capture-one.py <exp> <gain>
"""

import socket
import struct
import sys
import time

CTL = "/data/krux/camd-ctl.sock"


def addr_for(path):
    return bytes([1, 0] + list(path.encode()) + [0] * (108 - len(path)))


def ctl(cmd):
    """One command on the control socket.  It serves one command per
    connection and closes it afterwards."""
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(addr_for(CTL))
    s.settimeout(10)
    s.sendall(cmd)
    try:
        reply = s.recv(2)
    except OSError:
        reply = b""
    s.close()
    return reply


def main():
    exp = int(sys.argv[1], 0)
    gain = int(sys.argv[2], 0)

    r = ctl(b"E" + struct.pack(">HH", exp, gain))
    if r != b"OK":
        print("set_exposure failed: %r" % r)
        return 1
    # Let the picture settle at this exposure.
    time.sleep(1.2)

    if ctl(b"D") != b"OK":
        print("dump request failed")
        return 1
    # camd writes the dump from its capture thread.
    time.sleep(1.5)
    print("ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
