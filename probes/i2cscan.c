// What do the Nexus 5's I2C adapters support, and is anything on them?
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define I2C_SLAVE 0x0703
#define I2C_SLAVE_FORCE 0x0706
#define I2C_FUNCS 0x0705
#define I2C_RDWR 0x0707
#define I2C_SMBUS 0x0720
#define I2C_M_RD 0x0001
#define I2C_SMBUS_READ_BYTE_DATA 0x01

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

static void bus_info(int bus) {
    char path[32];
    snprintf(path, sizeof(path), "/dev/i2c-%d", bus);
    int fd = open(path, O_RDWR);
    if (fd < 0) {
        printf("bus %d: open: %s\n", bus, strerror(errno));
        return;
    }
    unsigned long funcs = 0;
    if (ioctl(fd, I2C_FUNCS, &funcs) < 0) {
        printf("bus %d: I2C_FUNCS: %s\n", bus, strerror(errno));
    } else {
        printf("bus %d: funcs=0x%lx", bus, funcs);
        struct {
            unsigned long bit;
            const char *name;
        } names[] = {
            {1UL << 0, "I2C_FUNC_I2C"}, {1UL << 1, "SMBUS_READ_BYTE"},
            {1UL << 2, "SMBUS_WRITE_BYTE"}, {1UL << 3, "SMBUS_READ_BYTE_DATA"},
            {1UL << 4, "SMBUS_WRITE_BYTE_DATA"}, {1UL << 5, "SMBUS_READ_WORD_DATA"},
            {1UL << 6, "SMBUS_WRITE_WORD_DATA"}, {1UL << 7, "SMBUS_READ_BLOCK_DATA"},
            {1UL << 8, "SMBUS_WRITE_BLOCK_DATA"}, {1UL << 9, "SMBUS_READ_I2C_BLOCK"},
            {1UL << 10, "SMBUS_WRITE_I2C_BLOCK"}, {1UL << 11, "SMBUS_EMUL"},
        };
        for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
            if (funcs & names[i].bit) {
                printf(" %s", names[i].name);
            }
        }
        printf("\n");
    }

    // scan every address with a one-byte read
    int found = 0;
    for (int addr = 0x03; addr <= 0x77; addr++) {
        unsigned char b = 0;
        if (ioctl(fd, I2C_SLAVE_FORCE, addr) < 0) {
            continue;
        }
        int n = read(fd, &b, 1);
        if (n == 1) {
            printf("  bus %d: addr 0x%02x read -> 0x%02x\n", bus, addr, b);
            found++;
        }
    }
    if (!found) {
        printf("  bus %d: nothing answered\n", bus);
    }
    close(fd);
}

int main(void) {
    int buses[] = {0, 2, 84};
    for (unsigned i = 0; i < sizeof(buses) / sizeof(buses[0]); i++) {
        bus_info(buses[i]);
    }
    return 0;
}
