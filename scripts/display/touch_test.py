import evdev, time
fd = evdev.open("/dev/input/event1")
print("the touch screen is open", flush := None)
end = time.ticks_add(time.ticks_ms(), 15000)
n = 0
while time.ticks_diff(end, time.ticks_ms()) > 0:
    e = evdev.read(fd, 300)
    if e is not None:
        print("  event: type=%d code=%d value=%d" % (e[0], e[1], e[2]), flush := None)
        n += 1
        if n > 12:
            break
print("%d events" % n)
