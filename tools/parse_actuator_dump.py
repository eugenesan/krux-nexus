#!/usr/bin/env python3
"""Parse the kernel's actuator configuration dump.

Written by actuator_dump_set_info() in
drivers/media/platform/msm/camera_v2/sensor/actuator/msm_actuator.c.

Layout (little-endian, 32-bit ARM):

    struct {
        u32 magic;            // 'ACT1'
        u32 reg_tbl_size;
        u32 init_size;
        u32 region_size;
        u32 data_size;
        u32 total_steps;
        u32 initial_code;
        u32 pwd_step;
        u32 i2c_addr;
        u32 addr_type;
        u32 data_type;
        u32 act_type;
    } hdr;
    struct msm_actuator_reg_params_t reg[reg_tbl_size];
    struct reg_settings_t init[init_size];
    struct region_params_t region[region_size];
"""

import ctypes
import struct
import sys


class Hdr(ctypes.Structure):
    _fields_ = [
        ("magic", ctypes.c_uint32),
        ("reg_tbl_size", ctypes.c_uint32),
        ("init_size", ctypes.c_uint32),
        ("region_size", ctypes.c_uint32),
        ("data_size", ctypes.c_uint32),
        ("total_steps", ctypes.c_uint32),
        ("initial_code", ctypes.c_int32),
        ("pwd_step", ctypes.c_uint32),
        ("i2c_addr", ctypes.c_uint32),
        ("addr_type", ctypes.c_uint32),
        ("data_type", ctypes.c_uint32),
        ("act_type", ctypes.c_uint32),
    ]


REG_PARAMS_SIZE = 16  # 4+4+2+2+2 + 2 pad
INIT_SETTING_SIZE = 4  # 2+2
REGION_PARAMS_SIZE = 6  # 2*2 + 2


def main(path):
    data = open(path, "rb").read()
    hdr = Hdr.from_buffer_copy(data[: ctypes.sizeof(Hdr)])
    if hdr.magic != 0x41435431:
        raise SystemExit("bad magic 0x%08x" % hdr.magic)

    print(
        "regs=%u init=%u regions=%u data_size=%u total_steps=%u "
        "initial_code=%d pwd_step=%u i2c=0x%02x addr_type=%u data_type=%u act_type=%u"
        % (
            hdr.reg_tbl_size,
            hdr.init_size,
            hdr.region_size,
            hdr.data_size,
            hdr.total_steps,
            hdr.initial_code,
            hdr.pwd_step,
            hdr.i2c_addr,
            hdr.addr_type,
            hdr.data_type,
            hdr.act_type,
        )
    )

    off = ctypes.sizeof(Hdr)
    print("reg table:")
    for i in range(hdr.reg_tbl_size):
        rec = data[off : off + REG_PARAMS_SIZE]
        write_type, hw_mask, reg_addr, hw_shift, data_shift = struct.unpack(
            "<IIHHH", rec[:14]
        )
        print(
            "  [%d] write_type=%u mask=0x%04x addr=0x%04x hw_shift=%u data_shift=%u"
            % (i, write_type, hw_mask, reg_addr, hw_shift, data_shift)
        )
        off += REG_PARAMS_SIZE

    print("init settings:")
    for i in range(hdr.init_size):
        addr, val = struct.unpack("<HH", data[off : off + 4])
        print("  [%d] 0x%04x = 0x%04x" % (i, addr, val))
        off += INIT_SETTING_SIZE

    print("regions:")
    for i in range(hdr.region_size):
        b0, b1, cps = struct.unpack("<HHH", data[off : off + 6])
        print("  [%d] macro_bound=%u inf_bound=%u code_per_step=%u" % (i, b0, b1, cps))
        off += REGION_PARAMS_SIZE

    print("bytes consumed %d of %d" % (off, len(data)))


if __name__ == "__main__":
    for arg in sys.argv[1:]:
        main(arg)
