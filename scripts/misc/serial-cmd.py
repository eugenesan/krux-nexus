#!/usr/bin/env python3
"""
serial-cmd.py -- send a few commands to the UART shell on the device.

The Nexus 5 kernel console and `start-serial` share ttyHSL0 (115200
8N1).  On the host that is usually /dev/ttyUSB0 (an FT232 adapter on
the headset jack).  This helper is for scripted checks when adb is off
(the sealed image) or broken.

    python3 scripts/misc/serial-cmd.py "id" "ps -A -o PID,NAME | grep adbd"

Every argument is sent as one command; the reply is printed between
markers.  Kernel messages interleave with the shell, which is expected.
No third-party modules: it uses termios directly.
"""
import argparse
import os
import sys
import termios
import time


def drain(fd, seconds):
    end = time.time() + seconds
    out = b""
    while time.time() < end:
        try:
            data = os.read(fd, 4096)
        except OSError:
            break
        if data:
            out += data
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument(
        "--tty",
        default=os.environ.get("KRUX_SERIAL", "/dev/ttyUSB0"),
        help="serial device (default: $KRUX_SERIAL or /dev/ttyUSB0)",
    )
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("commands", nargs="+", help="commands to run")
    args = parser.parse_args()

    try:
        fd = os.open(args.tty, os.O_RDWR | os.O_NOCTTY)
    except OSError as exc:
        sys.exit("serial-cmd: cannot open %s: %s" % (args.tty, exc))

    attrs = termios.tcgetattr(fd)
    attrs[0] = 0
    attrs[1] = 0
    attrs[2] = termios.CS8 | termios.CLOCAL | termios.CREAD
    attrs[3] = 0
    speed = getattr(termios, "B%d" % args.baud, None)
    if speed is None:
        sys.exit("serial-cmd: unsupported baud rate %d" % args.baud)
    attrs[4] = attrs[5] = speed
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 1
    termios.tcsetattr(fd, termios.TCSANOW, attrs)

    # Discard whatever the console printed before we started.
    drain(fd, 0.5)

    for command in args.commands:
        os.write(fd, command.encode() + b"\r\n")
        time.sleep(0.4)
        reply = drain(fd, 1.6)
        sys.stdout.write("### %s\n" % command)
        sys.stdout.write(reply.decode("utf-8", "replace"))
        sys.stdout.write("\n")

    os.close(fd)


if __name__ == "__main__":
    main()
