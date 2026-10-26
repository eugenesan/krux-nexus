import nfc, time
nfc.DEBUG = 0

SEL = bytes([0x00, 0xA4, 0x04, 0x00, 0x07,
             0xD2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01, 0x00])

t = nfc.transport()
try:
    for attempt in range(4):
        print("=== attempt %d ===" % (attempt + 1))
        t.chip = nfc.Chip().open()
        t._bring_up()
        try:
            ntf = t._discover(8000)
            t._activate(ntf)
            t.connected = True
            print("=== the card is there and activated ===")
            data, sw1, sw2 = t.transmit(SEL)
            print("=== the card answered: data=%s sw=%02x%02x"
                  % (data.hex() if data else "", sw1, sw2))
            break
        except Exception as e:
            print("    attempt %d: %s" % (attempt + 1, e))
        finally:
            t.close()
            t.connected = False
except Exception as e:
    print("failed: %s: %s" % (type(e).__name__, e))
