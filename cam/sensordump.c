/*
 * sensordump.c -- read the IMX179's registers while the camera is open.
 *
 * The imaging server can program the sensor and read it back, but it
 * also takes the camera over.  This one only reads: it opens the
 * sensor's sub-device and asks the kernel for the registers, so it can
 * be run *while the ROM's own camera app has the sensor open* -- that
 * is how the vendor's configuration is compared with ours.
 *
 *   sensordump <sensor subdev node>
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdint.h>

#define BASE_VIDIOC_PRIVATE  192
struct sensorb_cfg_data {
	int cfgtype;
	unsigned char cfg[76];
	unsigned int setting_size;
};
#define SENSOR_CFG_IOCTL \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 1, struct sensorb_cfg_data)
#define CFG_SLAVE_READ_I2C   1
#define MSM_CAMERA_I2C_BYTE_DATA 1
#define IMX179_SLAVE_ADDR    0x20

struct msm_camera_i2c_read_config {
	unsigned short slave_addr;
	unsigned short reg_addr;
	unsigned int data_type;
	unsigned short *data;
};

static int sensor_fd = -1;

static int read_reg(uint16_t reg, uint16_t *val)
{
	struct sensorb_cfg_data c;
	struct msm_camera_i2c_read_config rc;

	memset(&c, 0, sizeof(c));
	memset(&rc, 0, sizeof(rc));
	rc.slave_addr = IMX179_SLAVE_ADDR;
	rc.reg_addr = reg;
	rc.data_type = MSM_CAMERA_I2C_BYTE_DATA;
	rc.data = val;
	c.cfgtype = CFG_SLAVE_READ_I2C;
	/* the union member is a POINTER: the kernel copies the struct
	 * out of user space from there */
	*(void **)c.cfg = &rc;
	if (ioctl(sensor_fd, SENSOR_CFG_IOCTL, &c) < 0) {
		printf("the read of 0x%04x failed: %s\n", reg, strerror(errno));
		return -1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	static const uint16_t regs[] = {
		0x0100, 0x0101, 0x0102, 0x0105, 0x0106,
		0x0202, 0x0203, 0x0204, 0x0205,
		0x0301, 0x0302, 0x0303, 0x0304, 0x0305, 0x0306, 0x0307,
		0x0308, 0x0309, 0x030a, 0x030b, 0x030c, 0x030d,
		0x0340, 0x0341, 0x0342, 0x0343, 0x0344, 0x0345, 0x0346,
		0x0347, 0x0348, 0x0349, 0x034a, 0x034b, 0x034c, 0x034d,
		0x034e, 0x034f, 0x0350, 0x0351,
		0x0401, 0x0405, 0x0406, 0x0407,
		0x098e, 0x0990,
		0x3302, 0x3303, 0x33d4, 0x33d5, 0x33d6, 0x33d7,
	};
	const char *node = argc > 1 ? argv[1] : "/dev/v4l-subdev11";
	unsigned i;

	sensor_fd = open(node, O_RDWR);
	if (sensor_fd < 0) {
		printf("open %s: %s\n", node, strerror(errno));
		return 1;
	}
	printf("=== the sensor's registers (%s)\n", node);
	for (i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
		uint16_t v = 0;

		if (read_reg(regs[i], &v) < 0)
			break;
		printf("  0x%04x = 0x%04x\n", regs[i], v);
	}
	close(sensor_fd);
	return 0;
}
