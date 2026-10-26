import sys
sys.path.insert(0, "/data/local/tmp/kt")
print("1: the path", flush := None)
import board
print("2: the board", board.config["type"], flush := None)
import lcd
lcd.init()
print("3: the lcd is open:", lcd.width(), "x", lcd.height(), flush := None)
import pmu
print("4: the pmu", pmu.PMUController().get_battery_voltage(), "mV", flush := None)
from krux.power import power_manager
print("5: the power manager", flush := None)
from krux.display import display
print("6: the display object", flush := None)
display.initialize_lcd()
print("7: the lcd is initialised", flush := None)
