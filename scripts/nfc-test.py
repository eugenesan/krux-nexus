"""Read the NFC chip's id over the I2C bus: the transport check."""
import sys
sys.path.insert(0, ".")
from kruxport.i2c import I2CBus

bus = I2CBus(0)
print("bus 0 open")
# The BCM20793's register 0x07 is the chip id (0x13 for BCM2079x).
print("chip id: %02x" % bus.readfrom(0x77, 1)[0])
print("regs 0x00..0x07:", " ".join("%02x" % b for b in bus.readfrom(0x77, 8)))
