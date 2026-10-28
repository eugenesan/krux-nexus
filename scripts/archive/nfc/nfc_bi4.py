import nfc, time
nfc.DEBUG = 1
t = nfc.transport()
t.chip = nfc.Chip().open()
print("A: calling _bring_up", flush := None)
t._bring_up()
print("B: _bring_up returned")
