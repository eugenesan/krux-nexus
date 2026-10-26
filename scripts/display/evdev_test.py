import evdev, time

# the touch screen
fd = evdev.open("/dev/input/event1")
print("the touch screen is open")
end = time.ticks_add(time.ticks_ms(), 12000)
n = 0
while time.ticks_diff(end, time.ticks_ms()) > 0:
    e = evdev.read(fd, 200)
    if e is not None:
        t, c, v = e
        if True:
            print("  touch: type=%d code=%d value=%d" % (t, c, v))
            n += 1
        if n > 12:
            break
evdev.close(fd)
print("the touch screen: %d events" % n)

# the phone's keys
for name, path in (("the power key", "/dev/input/event0"),
                   ("the volume keys", "/dev/input/event2")):
    fd = evdev.open(path)
    end = time.ticks_add(time.ticks_ms(), 8000)
    got = 0
    while time.ticks_diff(end, time.ticks_ms()) > 0:
        e = evdev.read(fd, 200)
        if e is not None and e[0] == evdev.EV_KEY:
            print("  %s: code=%d value=%d" % (name, e[1], e[2]))
            got += 1
    evdev.close(fd)
    print("%s: %d key events" % (name, got))
