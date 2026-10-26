#!/usr/bin/env python3
"""fb2png.py -- turn a raw framebuffer dump into a PNG, so it can be looked at.

The device's framebuffer is 1080 pixels wide with a stride of 4352 bytes
(1080 * 4 = 4320, so there are 32 bytes of padding per line).  The pixel
layout is what the kernel reports through FBIOGET_VSCREENINFO: on this
device the red and blue offsets are swapped relative to what the panel
shows, so the colours are exchanged here as well, to show what the panel
would show.

Usage: fb2png.py <raw> <out.png> [buffer-index]
"""

import sys

from PIL import Image

STRIDE = 4352
WIDTH = 1080
HEIGHT = 1920


def main() -> int:
    raw = open(sys.argv[1], "rb").read()
    out = sys.argv[2]
    index = int(sys.argv[3]) if len(sys.argv) > 3 else 0

    img = Image.new("RGB", (WIDTH, HEIGHT))
    pixels = img.load()

    for y in range(HEIGHT):
        row = y * STRIDE
        for x in range(WIDTH):
            off = row + x * 4
            if off + 4 > len(raw):
                break
            b, g, r, a = raw[off], raw[off + 1], raw[off + 2], raw[off + 3]
            # the panel shows red and blue the other way round
            pixels[x, y] = (b, g, r)

    img.save(out)
    print("  wrote %s (%dx%d)" % (out, WIDTH, HEIGHT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
