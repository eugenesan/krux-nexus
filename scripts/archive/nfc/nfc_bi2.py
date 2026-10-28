import nfc, time
nfc.DEBUG = 1
c = nfc.Chip().open()
c.notifications = []
c.power_cycle()
c.wake(1)
print("A: the core reset", c.read(3000))
time.sleep(0.3)
steps = [
    ("GET_BUILD_INFO", nfc.MT_CMD, nfc.GID_PROP, nfc.PROP_GET_BUILD_INFO, b""),
    ("GET_PATCH_VERSION", nfc.MT_CMD, nfc.GID_PROP, nfc.PROP_GET_PATCH_VERSION, b""),
    ("CORE_RESET", nfc.MT_CMD, nfc.GID_CORE, nfc.CORE_RESET, bytes([1])),
    ("CORE_INIT", nfc.MT_CMD, nfc.GID_CORE, nfc.CORE_INIT, b""),
    ("CFG_LPTD", nfc.MT_CMD, nfc.GID_CORE, nfc.CORE_SET_CONFIG, nfc.SET_CONFIG_LPTD),
    ("CFG_STARTUP", nfc.MT_CMD, nfc.GID_CORE, nfc.CORE_SET_CONFIG, nfc.SET_CONFIG_START_UP),
    ("CFG_POLL", nfc.MT_CMD, nfc.GID_CORE, nfc.CORE_SET_CONFIG, nfc.SET_CONFIG_POLL),
    ("PROP06", nfc.MT_CMD, nfc.GID_PROP, 0x06, nfc.PROP_06),
    ("DISCOVER", nfc.MT_CMD, nfc.GID_RF, nfc.RF_DISCOVER, nfc.DISCOVER),
    ("CFG_RF51", nfc.MT_CMD, nfc.GID_CORE, nfc.CORE_SET_CONFIG, nfc.SET_CONFIG_RF),
    ("SET_CLK", nfc.MT_CMD, nfc.GID_CORE, nfc.CORE_SET_CLK, nfc.SET_CLK),
    ("PROP0C", nfc.MT_CMD, nfc.GID_PROP, 0x0C, nfc.PROP_0C),
    ("ROUTING", nfc.MT_CMD, nfc.GID_RF, nfc.RF_SET_ROUTING, nfc.RF_ROUTING),
    ("CFG19", nfc.MT_CMD, nfc.GID_CORE, nfc.CORE_SET_CONFIG, nfc.SET_CONFIG_19),
    ("CFG80", nfc.MT_CMD, nfc.GID_CORE, nfc.CORE_SET_CONFIG, nfc.SET_CONFIG_80),
    ("CFGPOLL", nfc.MT_CMD, nfc.GID_CORE, nfc.CORE_SET_CONFIG, nfc.SET_CONFIG_POLL_PARAMS),
    ("CFG00", nfc.MT_CMD, nfc.GID_CORE, nfc.CORE_SET_CONFIG, nfc.SET_CONFIG_00),
    ("PROTOCOLS", nfc.MT_CMD, nfc.GID_RF, nfc.RF_PROTOCOLS_OID, nfc.RF_PROTOCOLS),
    ("DEACTIVATE", nfc.MT_CMD, nfc.GID_RF, nfc.RF_DEACTIVATE, bytes([0])),
]
for name, mt, gid, oid, payload in steps:
    print("-> " + name)
    r = c.send(mt, gid, oid, payload)
    print("   " + name + " ok")
c.close()
print("ALL DONE")
