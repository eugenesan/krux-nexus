import nfc, time
nfc.DEBUG = 1
print("1")
t = nfc.transport()
print("2")
t.chip = nfc.Chip().open()
print("3")
chip = t.chip
chip.notifications = []
print("4")
chip.power_cycle()
print("5")
chip.wake(1)
print("6")
ntf = chip.read(5000)
print("7", ntf)
time.sleep(0.3)
print("8")
chip.send(nfc.MT_CMD, nfc.GID_PROP, nfc.PROP_GET_BUILD_INFO)
print("9")
