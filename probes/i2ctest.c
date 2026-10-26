// Probe the Nexus 5's I2C buses with the i2c-dev ABI, to pin down the
// NFC chip's bus and address before writing the MicroPython module.
//
// The i2c-dev ABI is stable, and the build sysroot has no kernel
// headers, so it is spelled out here (from linux/i2c-dev.h and
// linux/i2c.h):
//
//   I2C_SLAVE 0x0703   I2C_RDWR 0x0707   I2C_SMBUS 0x0720
//   I2C_M_RD  0x0001
//   struct i2c_msg { __u16 addr; __u16 flags; __u16 len; __u8 *buf; }
//
// Note that on 32-bit ARM the pointer in struct i2c_msg needs 4-byte
// alignment, so the struct is 12 bytes with 2 bytes of padding after
// the three __u16 fields.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define I2C_SLAVE 0x0703
#define I2C_RDWR 0x0707
#define I2C_SMBUS 0x0720
#define I2C_M_RD 0x0001
#define I2C_SMBUS_READ_BYTE_DATA 0x01
#define I2C_SMBUS_WRITE_BYTE_DATA 0x02

typedef unsigned short __u16;
typedef unsigned int __u32;

struct i2c_msg {
    __u16 addr;
    __u16 flags;
    __u16 len;
    unsigned char *buf;
};

struct i2c_rdwr_ioctl_data {
    struct i2c_msg *msgs;
    __u32 nmsgs;
};

static void dump(const char *what, const unsigned char *b, int n) {
    printf("    %-22s:", what);
    for (int i = 0; i < n; i++) {
        printf(" %02x", b[i]);
    }
    printf("\n");
}

static void try_bus(int bus, int addr) {
    char path[32];
    snprintf(path, sizeof(path), "/dev/i2c-%d", bus);
    int fd = open(path, O_RDWR);
    if (fd < 0) {
        printf("  bus %d: cannot open (%s)\n", bus, strerror(errno));
        return;
    }
    printf("  bus %d, slave 0x%02x:\n", bus, addr);

    // (a) a plain read of the current register
    unsigned char buf[16];
    memset(buf, 0, sizeof(buf));
    if (ioctl(fd, I2C_SLAVE, addr) >= 0) {
        int n = read(fd, buf, 1);
        if (n >= 0) {
            dump("plain read 1", buf, 1);
        } else {
            printf("    plain read 1: %s\n", strerror(errno));
        }
    } else {
        printf("    I2C_SLAVE: %s\n", strerror(errno));
    }

    // (b) write the register, then read it back (repeated start)
    for (int reg = 0; reg <= 0x07; reg++) {
        struct i2c_msg msgs[2];
        struct i2c_rdwr_ioctl_data d;
        unsigned char regbuf = reg;
        memset(buf, 0, sizeof(buf));

        msgs[0].addr = addr;
        msgs[0].flags = 0;
        msgs[0].len = 1;
        msgs[0].buf = &regbuf;
        msgs[1].addr = addr;
        msgs[1].flags = I2C_M_RD;
        msgs[1].len = 8;
        msgs[1].buf = buf;

        d.msgs = msgs;
        d.nmsgs = 2;
        if (ioctl(fd, I2C_RDWR, &d) < 0) {
            printf("    reg 0x%02x rdwr: %s\n", reg, strerror(errno));
        } else {
            char what[32];
            snprintf(what, sizeof(what), "reg 0x%02x read 8", reg);
            dump(what, buf, 8);
        }
    }

    // (c) SMBus read byte data
    for (int reg = 0; reg <= 0x07; reg++) {
        unsigned char sb[3];
        memset(sb, 0, sizeof(sb));
        sb[0] = I2C_SMBUS_READ_BYTE_DATA;
        sb[1] = reg;
        if (ioctl(fd, I2C_SMBUS, sb) < 0) {
            printf("    reg 0x%02x smbus: %s\n", reg, strerror(errno));
        } else {
            char what[32];
            snprintf(what, sizeof(what), "reg 0x%02x smbus", reg);
            dump(what, &sb[2], 1);
        }
    }

    close(fd);
}

int main(void) {
    printf("I2C_RDWR=0x%x I2C_SLAVE=0x%x sizeof(rdwr_data)=%zu sizeof(i2c_msg)=%zu\n",
        (unsigned)I2C_RDWR, (unsigned)I2C_SLAVE, sizeof(struct i2c_rdwr_ioctl_data),
        sizeof(struct i2c_msg));
    int buses[] = {0, 2, 84};
    int addrs[] = {0x77, 0x34};
    for (unsigned i = 0; i < sizeof(buses) / sizeof(buses[0]); i++) {
        for (unsigned j = 0; j < sizeof(addrs) / sizeof(addrs[0]); j++) {
            try_bus(buses[i], addrs[j]);
        }
    }
    return 0;
}
