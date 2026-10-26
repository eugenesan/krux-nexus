# the port's transport, used exactly as Krux's seedkeeper.py uses it
import nfc
t = nfc.transport()
try:
    t.connect(40000)
    print("connected:", t.is_connected())
    sel = bytes([0x00, 0xA4, 0x04, 0x00, 0x07,
                 0xD2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01, 0x00])
    data, sw1, sw2 = t.transmit(sel)
    print("the card answered: data=%s sw=%02x%02x"
          % (data.hex() if data else "", sw1, sw2))
finally:
    t.close()
    print("closed")
