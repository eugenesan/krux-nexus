import _nfc, time

def hexs(b):
    return " ".join("%02x" % c for c in b)

fd = _nfc.open()
print("opened /dev/bcm2079x -> fd", fd)
# the wake line is deasserted at power-on (NFC_WAKE_ASSERTED_ON_POR == UPIO_OFF)
_nfc.wake(fd, 0)
_nfc.power(fd, 0)
time.sleep(0.05)
_nfc.power(fd, 1)
print("power cycled")
pkt = _nfc.read(fd, 6000)
print("packet after power on:", hexs(pkt) if pkt else "(nothing)")
n = _nfc.write(fd, bytes([0x10, 0x2f, 0x04, 0x00]))
print("wrote", n, "bytes (GET_BUILD_INFO)")
rsp = _nfc.read(fd, 6000)
print("reply:", hexs(rsp) if rsp else "(nothing)")
if rsp:
    print("  as text:", "".join(chr(c) if 32 <= c < 127 else "." for c in rsp[4:]))
_nfc.power(fd, 0)
print("powered off")
_nfc.close(fd)
