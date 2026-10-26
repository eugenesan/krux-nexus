#!/sbin/sh
#
# imaging.sh -- run the imaging server with the settings that are known
#               to be right.
#
# The imaging server takes its settings as positional arguments, which is
# easy to get wrong: one argument too few and every later setting shifts
# by one, and the server then says so in its summary.  This script names
# the settings instead, so the command line cannot be miscounted.
#
#   imaging.sh <program> [live]
#
# The settings, in the order the program takes them:
#
#    1  3280        the width the camera front end expects
#    2  2464        the height
#    3  0x44000000  the ion heap mask
#    4  rear        the camera
#    5  0           the front camera's commands
#    6  the CPP's firmware
#    7  0           the ISP's stream source: CAMIF_RAW
#    8  0           the binning mode
#    9  0           the initial configuration
#   10  0           the per-frame cycle
#   11  0           the sensor's virtual channel
#   12  0x2b        the data type on the routed channel
#   13  0           the CID the ISP-IF routes to PIX0
#   14  0           the CSID
#   15  1           the frames to wait for
#   16  0           the ISP's VFE
#   17  0           the ISP-IF's VFE
#   18  0           the pixel clock (0 = the default)
#   19  0x12        the data type on the other channel
#   20  0           the CSID's LUT: cid 0
#   21  1           the CSID's LUT: cid 1
#   22  0xffffffff  the register to poke (none)
#   23  0           the value to poke
#   24  0           both CIDs on the ISP-IF
#   25  live        how the sensor's registers are programmed
#   26  crop        whether the ISP-IF's crop is enabled
#   27  src         the ISP's stream source: 0 = PIX_ENCODER,
#                   1 = PIX_VIEWFINDER, 2 = CAMIF_RAW, 3 = IDEAL_RAW
#   28  capture     send the vendor's capture configuration as well
#
# The CSID's lookup table is the vendor's: virtual channel 0 carries the
# data type 0x2b (the image) and virtual channel 1 the data type 0x12
# (the embedded data).  Measured under our kernel, the vendor's own
# session leaves the CSID's LUT at 0x0000122b with cfg0 0x23 and
# cfg1 0x13 -- exactly what this table produces.
#
# "live" is how the sensor's registers are programmed:
#
#   1  the register list read out of the sensor while the vendor's
#      camera app had it open (standalone/cam/sensor_live.h)
#   2  the captured page sequence first, then that list
#
# The shebang is /sbin/sh, not /bin/sh: the stub ramdisk has no /bin,
# and on the ROM's Android /sbin is a link to /system/bin.
#
set -e

PROG=${1:-/data/local/tmp/imaging11}
MODE=${2:-1}
CROP=${3:-1}
SRC=${4:-2}
CAP=${5:-1}
FW=${6:-/lib/firmware/cpp_firmware_v1_2_0.fw}

# The arguments are listed one per line, without comments on the same
# line: a backslash-continued comment would swallow the rest of the
# command.
exec "$PROG" \
3280 \
2464 \
0x44000000 \
rear \
0 \
"$FW" \
"$SRC" \
0 \
0 \
0 \
0 \
0x2b \
0 \
0 \
1 \
0 \
0 \
0 \
0x12 \
0 \
1 \
0xffffffff \
0 \
0 \
"$MODE" \
"$CROP" \
"$CAP"
