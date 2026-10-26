#!/usr/bin/env python3
"""Add the I2C bus to MicroPython's machine module, for the Linux port.

Krux's firmware uses MaixPy's machine.I2C (see krux/src/krux/i2c.py:
I2C(I2C.I2C0, freq=400000, scl=..., sda=...)).  On the Nexus 5 the
peripherals are on the kernel's I2C bus, so the same class has to talk
to /dev/i2c-N with the I2C_RDWR ioctl.

The class is compiled into the port's machine module (extmod/modmachine.c
includes ports/unix/modmachine.c and has the MICROPY_PY_MACHINE_I2C
hook), so the firmware's "from machine import I2C" works unchanged.
"""
import os
import re

PORT = "/tmp/opencode/mp/micropython-1.28.0/ports/unix"

C_CODE = r'''
/*
 * The I2C bus, for the Nexus 5 (the Linux port).
 *
 * The peripherals (the NFC chip, the PMU) are on the kernel's I2C bus,
 * so this is the same bus seen from userspace: /dev/i2c-N and the
 * I2C_RDWR ioctl.  The API is MaixPy's, because Krux's firmware uses
 * it: I2C(I2C.I2C0, freq=400000, scl=..., sda=...).
 */
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

typedef struct _mp_obj_machine_i2c_t {
    mp_obj_base_t base;
    int fd;
} mp_obj_machine_i2c_t;

STATIC mp_obj_t machine_i2c_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    static const mp_arg_check_t allowed[] = {
        { MP_QSTR_id, MP_ROM_INT(&mp_type_int) },
        { MP_QSTR_freq, MP_ROM_INT(&mp_type_int) },
        { MP_QSTR_scl, MP_ROM_NONE },
        { MP_QSTR_sda, MP_ROM_NONE },
    };
    mp_arg_check_num(n_args, n_kw, 1, true, 4);
    mp_map_t kw_args;
    mp_map_init_fixed_table(&kw_args, MP_ARRAY_SIZE(allowed), allowed);
    mp_arg_parse_all(n_args, n_kw, args, MP_ARRAY_SIZE(allowed), &kw_args);

    int bus = mp_obj_get_int(args[0]);
    mp_obj_machine_i2c_t *self = mp_obj_malloc(type, sizeof(mp_obj_machine_i2c_t));
    char path[32];
    snprintf(path, sizeof(path), "/dev/i2c-%d", bus);
    self->fd = open(path, O_RDWR);
    if (self->fd < 0) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("cannot open the I2C bus"));
    }
    return MP_OBJ_FROM_PTR(self);
}

STATIC void machine_i2c_print(const mp_print_t *print, const mp_obj_t *self_in, mp_print_kind_t kind) {
    (void)self_in;
    (void)kind;
    mp_print_str(print, "I2C()");
}

// write a register block and read a block back, in one transaction
STATIC int machine_i2c_xfer(mp_obj_machine_i2c_t *self, int addr, const uint8_t *w, size_t wlen, uint8_t *r, size_t rlen) {
    uint8_t buf[512];
    if (wlen + rlen + 1 > sizeof(buf)) {
        return -1;
    }
    buf[0] = (uint8_t)addr;
    if (wlen) {
        memcpy(&buf[1], w, wlen);
    }
    struct i2c_msg msgs[2];
    int nmsgs = 0;
    if (wlen) {
        msgs[nmsgs].addr = addr;
        msgs[nmsgs].flags = 0;
        msgs[nmsgs].len = (uint16_t)wlen;
        msgs[nmsgs].buf = (uint8_t *)w;
        nmsgs++;
    }
    msgs[nmsgs].addr = addr;
    msgs[nmsgs].flags = I2C_M_RD;
    msgs[nmsgs].len = (uint16_t)rlen;
    msgs[nmsgs].buf = r;
    nmsgs++;
    struct i2c_rdwr_ioctl_data data;
    data.msgs = msgs;
    data.nmsgs = (uint32_t)nmsgs;
    if (ioctl(self->fd, I2C_RDWR, &data) < 0) {
        return -1;
    }
    return 0;
}

STATIC mp_obj_t machine_i2c_readfrom(mp_obj_t self_in, mp_obj_t addr_in, mp_obj_t nbytes_in) {
    mp_obj_machine_i2c_t *self = MP_OBJ_TO_PTR(self_in);
    int addr = mp_obj_get_int(addr_in);
    size_t n = mp_obj_get_int(nbytes_in);
    vstr_t vstr;
    bytearray *out = vstr_new_bytearray(n, NULL);
    if (machine_i2c_xfer(self, addr, NULL, 0, (uint8_t *)out->items, n) < 0) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("I2C read failed"));
    }
    return MP_OBJ_FROM_PTR(out);
}

STATIC mp_obj_t machine_i2c_writeto(mp_obj_t self_in, mp_obj_t addr_in, mp_obj_t data_in) {
    mp_obj_machine_i2c_t *self = MP_OBJ_TO_PTR(self_in);
    int addr = mp_obj_get_int(addr_in);
    size_t len;
    const char *data = mp_obj_get_buffer_raise(data_in, &len);
    if (machine_i2c_xfer(self, addr, (const uint8_t *)data, len, NULL, 0) < 0) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("I2C write failed"));
    }
    return mp_const_none;
}

STATIC mp_obj_t machine_i2c_readfrom_mem(mp_obj_t self_in, mp_obj_t addr_in, mp_obj_t mem_in, mp_obj_t nbytes_in) {
    mp_obj_machine_i2c_t *self = MP_OBJ_TO_PTR(self_in);
    int addr = mp_obj_get_int(addr_in);
    int mem = mp_obj_get_int(mem_in);
    size_t n = mp_obj_get_int(nbytes_in);
    uint8_t w[1];
    vstr_t vstr;
    bytearray *out = vstr_new_bytearray(n, NULL);
    w[0] = (uint8_t)mem;
    if (machine_i2c_xfer(self, addr, w, 1, (uint8_t *)out->items, n) < 0) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("I2C read failed"));
    }
    return MP_OBJ_FROM_PTR(out);
}

STATIC mp_obj_t machine_i2c_writeto_mem(mp_obj_t self_in, mp_obj_t addr_in, mp_obj_t mem_in, mp_obj_t data_in) {
    mp_obj_machine_i2c_t *self = MP_OBJ_TO_PTR(self_in);
    int addr = mp_obj_get_int(addr_in);
    int mem = mp_obj_get_int(mem_in);
    size_t len;
    const char *data = mp_obj_get_buffer_raise(data_in, &len);
    uint8_t w[256];
    if (len + 1 > sizeof(w)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("I2C write too long"));
    }
    w[0] = (uint8_t)mem;
    memcpy(&w[1], data, len);
    if (machine_i2c_xfer(self, addr, w, len + 1, NULL, 0) < 0) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("I2C write failed"));
    }
    return mp_const_none;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_2(machine_i2c_readfrom_obj, machine_i2c_readfrom);
STATIC MP_DEFINE_CONST_FUN_OBJ_2(machine_i2c_writeto_obj, machine_i2c_writeto);
STATIC MP_DEFINE_CONST_FUN_OBJ_3(machine_i2c_readfrom_mem_obj, machine_i2c_readfrom_mem);
STATIC MP_DEFINE_CONST_FUN_OBJ_3(machine_i2c_writeto_mem_obj, machine_i2c_writeto_mem);

STATIC const mp_rom_map_elem_t machine_i2c_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_readfrom), MP_ROM_PTR(&machine_i2c_readfrom_obj) },
    { MP_ROM_QSTR(MP_QSTR_writeto), MP_ROM_PTR(&machine_i2c_writeto_obj) },
    { MP_ROM_QSTR(MP_QSTR_readfrom_mem), MP_ROM_PTR(&machine_i2c_readfrom_mem_obj) },
    { MP_ROM_QSTR(MP_QSTR_writeto_mem), MP_ROM_PTR(&machine_i2c_writeto_mem_obj) },
};
STATIC MP_DEFINE_CONST_DICT(machine_i2c_locals_dict, machine_i2c_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(machine_i2c_type,
    MP_QSTR_I2C,
    MP_TYPE_FLAG_NONE,
    make_new, machine_i2c_make_new,
    print, machine_i2c_print,
    locals_dict, &machine_i2c_locals_dict);
'''


def main():
    p = os.path.join(PORT, "modmachine.c")
    s = open(p).read()
    if "machine_i2c_type" in s:
        print("  the I2C class is already there")
    else:
        # the port's file is included inside extmod/modmachine.c, so the
        # class has to be defined before the module's table
        marker = "#include <py/obj.h>"
        if marker not in s:
            marker = "#if MICROPY_PLAT_DEV_MEM"
        s = s.replace(marker, C_CODE + "\n" + marker, 1)
        open(p, "w").write(s)
        print("  the I2C class added to the port's machine module")

    # the Makefile: the class needs the extmod hook
    mk = os.path.join(PORT, "Makefile")
    s = open(mk).read()
    if "MICROPY_PY_MACHINE_I2C" not in s:
        s = s.replace("CFLAGS += -DMICROPY_PY_TERMIOS=1",
                      "CFLAGS += -DMICROPY_PY_TERMIOS=1\nCFLAGS += -DMICROPY_PY_MACHINE_I2C=1", 1)
        open(mk, "w").write(s)
        print("  the Makefile enables the I2C hook")


main()
