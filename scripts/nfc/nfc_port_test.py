"""Try the port's own NFC transport against a card.

This is the port's `nfc` module doing the whole thing on its own: the
chip's bring-up, the polling, the activation and one APDU exchange.
"""

import nfc

# the SeedKeeper's SELECT by DF name (the abbreviated AID), as the
# phone's own NFC stack sends it
SELECT = bytes([0x00, 0xA4, 0x04, 0x00, 0x07,
                0xD2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01, 0x00])

print("transport:", nfc.transport())
t = nfc.transport()
try:
    t.connect(30000)
    print("connected:", t.is_connected())
    data, sw1, sw2 = t.transmit(SELECT)
    print("the card answered: data=%s sw=%02x%02x" % (data.hex(), sw1, sw2))
except Exception as e:  # noqa: BLE001
    print("failed: %s: %s" % (type(e).__name__, e))
finally:
    t.close()
print("done")
