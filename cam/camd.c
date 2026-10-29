/*
 * imaging8.c -- the imaging server, with both cameras.
 *
 * Same as imaging7.c, but the per-camera parameters are selected at
 * runtime, and the front camera (MT9M114B) is supported as well:
 *
 *   rear  (IMX179)    the sensor's mode registers are not in any kernel,
 *                     so they are lifted out of the vendor's sensor
 *                     library by tools/gen-imx179-regs.py and written
 *                     with CFG_WRITE_I2C_ARRAY.
 *
 *   front (MT9M114B)  the kernel's driver has the tables
 *                     (mt9m114_recommend_settings, mt9m114_720p_settings,
 *                     mt9m114_config_change_settings) and implements the
 *                     sensor config ioctls itself, so *nothing* has to be
 *                     sent: CFG_SET_INIT_SETTING, CFG_SET_RESOLUTION and
 *                     CFG_SET_START_STREAM are enough -- no vendor data
 *                     at all.
 *
 * The front camera is the interesting test: it tells us whether the
 * pipeline (CSID -> ISP -> buffers -> DQBUF) completes on our own
 * kernel, with the sensor part taken care of by the kernel itself.
 *
 * Run:
 *   imaging8 [width] [height] [ion-heap-mask] [front|rear]
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/un.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include "v4l2_caf.h"
#include "imx179_regs.h"
#include "isp.h"
#include "ispif.h"
#include "ion.h"
#include "mipi.h"
#include "sensor.h"
#include "cam_config.h"
#include "cam_capture.h"
#include "cam_cycle.h"
#include "cam_chromatrix.h"
#include "sensor_config.h"
#include "sensor_live.h"

/* the CPP (colour processing) firmware: a 16 KB blob the kernel loads
 * through the firmware loader, named by VIDIOC_MSM_CPP_LOAD_FIRMWARE */
struct msm_camera_v4l2_ioctl_t {
	unsigned int id;
	unsigned int len;
	unsigned int trans_code;
	void *ioctl_ptr;
};

#define VIDIOC_MSM_CPP_LOAD_FIRMWARE \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 3, struct msm_camera_v4l2_ioctl_t)
#define CPP_NODE "/dev/v4l-subdev17"

#define MSM_CAMERA_V4L2_EVENT_TYPE (0x08000000 + 0x00002000)
#define MSM_CAMERA_NEW_SESSION     1
#define MSM_CAMERA_DEL_SESSION     2
#define MSM_CAMERA_SET_PARM        3
#define MSM_CAMERA_GET_PARM        4
#define MSM_CAMERA_MSM_NOTIFY      7
#define MSM_CAMERA_CMD_SUCESS      0x00000001

#define PRIV_S_CROP        (0x08000000 + 1)
#define PRIV_G_CROP        (0x08000000 + 2)
#define PRIV_G_FMT         (0x08000000 + 3)
#define PRIV_S_FMT         (0x08000000 + 4)
#define PRIV_TRY_FMT       (0x08000000 + 5)
#define PRIV_METADATA      (0x08000000 + 6)
#define PRIV_QUERY_CAP     (0x08000000 + 7)
#define PRIV_STREAM_ON     (0x08000000 + 8)
#define PRIV_STREAM_OFF    (0x08000000 + 9)
#define PRIV_NEW_STREAM    (0x08000000 + 10)
#define PRIV_DEL_STREAM    (0x08000000 + 11)
#define PRIV_SHUTDOWN      (0x08000000 + 12)

/* the msm-config node: the event channel */
#define CONFIG_NODE "/dev/video0"

/* the format the camera node wants: struct msm_v4l2_format_data */
struct msm_v4l2_format_data {
	unsigned int type;
	unsigned int width;
	unsigned int height;
	unsigned int pixelformat;
	unsigned char num_planes;
	unsigned char pad[3];
	unsigned int plane_sizes[8];
};

#define EVSZ 128
#define DQ_IOCTL  _IOR('V', 89, char[EVSZ])
#define ACK_IOCTL _IOW('V', 224, char[EVSZ])

#define OFF_CMD     8
#define OFF_STATUS  12
#define OFF_SESSION 16
#define OFF_STREAM  20
#define OFF_ARG     36
#define OFF_RET     40
#define OFF_ID      88

/* the sensor config ioctls (include/media/msm_cam_sensor.h) */
#define CFG_SET_SLAVE_INFO       0
#define CFG_SLAVE_READ_I2C       1
#define CFG_WRITE_I2C_ARRAY      2
#define CFG_SLAVE_WRITE_I2C_ARRAY 3
#define CFG_WRITE_I2C_SEQ_ARRAY  4
#define CFG_POWER_UP             5
#define CFG_POWER_DOWN           6
#define CFG_SET_STOP_STREAM_SETTING 7
#define CFG_GET_SENSOR_INFO      8
#define CFG_GET_SENSOR_INIT_PARAMS 9
#define CFG_SET_INIT_SETTING     10
#define CFG_SET_RESOLUTION       11
#define CFG_SET_STOP_STREAM      12
#define CFG_SET_START_STREAM     13

struct sensorb_cfg_data {
	int cfgtype;
	unsigned char cfg[76];
	unsigned int setting_size;
};

#define SENSOR_CFG_IOCTL \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 1, struct sensorb_cfg_data)

/* ------------------------------------------------------------------ */
struct camera {
	const char *name;
	int		binned;		/* use the 1632x1224 binning mode */
	const char *sensor_node;	/* the msm-sensor video node */
	const char *sensor_subdev;	/* the sensor's subdev */
	const char *csiphy_node;
	const char *csid_node;
	int		csid_core;	/* which CSID the PHY talks to */
	int		width, height;	/* the sensor's frame geometry */
	unsigned int	pix_clk;
	unsigned int	ion_mask;
	int		kernel_programs_sensor;	/* MT9M114B does it itself */
};

static const struct camera cam_rear = {
	.name = "rear",
	.sensor_node = "/dev/video1",
	.sensor_subdev = "/dev/v4l-subdev11",
	.csiphy_node = "/dev/v4l-subdev0",
	.csid_node = "/dev/v4l-subdev3",
	.csid_core = 0,
	/* the IMX179's HMAX/VMAX, read back from the sensor: 3280x2464
	 * (the active area is 3264x2448, the rest is blanking) */
	.width = 3280, .height = 2464,
	.pix_clk = 260000000,	/* the value the vendor's HAL uses */
	.binned = 0,
	.ion_mask = ION_MASK_SYSTEM,
	.kernel_programs_sensor = 0,
};

/* the rear camera in its 1632x1224 binning mode */
static struct camera cam_binned = {
	.name = "rear-binned",
	.sensor_node = "/dev/video1",
	.sensor_subdev = "/dev/v4l-subdev11",
	.csiphy_node = "/dev/v4l-subdev0",
	.csid_node = "/dev/v4l-subdev3",
	.csid_core = 0,
	/* HMAX 1640, VMAX 1232 */
	.width = 1640, .height = 1232,
	.pix_clk = 60600000,	/* 1640 * 1232 * 30 */
	.ion_mask = ION_MASK_SYSTEM,
	.binned = 1,
	.kernel_programs_sensor = 0,
};

static const struct camera cam_front = {
	.name = "front",
	.sensor_node = "/dev/video2",
	.sensor_subdev = "/dev/v4l-subdev12",
	.csiphy_node = "/dev/v4l-subdev2",
	.csid_node = "/dev/v4l-subdev5",
	.csid_core = 2,
	.width = 1280, .height = 720,
	.pix_clk = 0,
	.ion_mask = ION_MASK_SYSTEM,
	.kernel_programs_sensor = 1,
};

static const struct camera *cam = &cam_rear;
/* The ION heap masks are masks of heap IDS, not of heap types: the
 * "system" heap is id 30, so its mask is 1 << 30.  The old value
 * (0x10000, bit 16) is the "CP writeback" heap, which is a 8660-only
 * heap -- on this 8974 the allocation fails with ENOENT. */
static unsigned int ion_mask_override = 0;

/* A register to write after the vendor's sequence, to try a value the
 * sequence does not set.  The sensor's virtual channel is such a
 * register: the packets arrive on VC 1 under our kernel and on VC 0
 * in the ROM, and the sequence is the same in both, so the register
 * that decides it is one the sequence does not write.  0xffffffff
 * means "do not poke". */
static unsigned int poke_reg = 0xffffffff;

/* Whether the ISP-IF should be given both virtual channels. */
static int two_cids = 0;

/*
 * Whether the ISP-IF's crop is enabled.
 *
 * The crop is what switches the ISP's pixel input on: msm_ispif_enable_crop()
 * -- the function that sets the PIX0 enable bit in CTRL_0 -- is only called
 * when crop_enable is set, so without it the pixel input stays off.
 *
 * The vendor's own camera HAL, however, sends crop_enable 0 with the
 * window 0..0 (measured under our kernel, in the kernel's own print of
 * the routing), and it works.  So the crop is a knob, not a fact.
 */
static int crop_enable = 1;

/*
 * Send the vendor's *capture* configuration as well.
 *
 * The vendor's camera uploads its ISP configuration on every frame, and
 * the preview's and the capture's are not the same: the capture sets the
 * SFE's and the CAMIF's geometry for the photograph (0x444 = 0x403f and
 * 0x464 = 0x30001f5), while the preview's are the viewfinder's
 * (0x444 = 0x6020 and 0x464 = 0x18000fc).  The camera page needs the
 * capture's.
 */
static int send_capture_config = 1;
static unsigned int poke_val = 0;
/* the camera node's session id (the video node number), taken from the
 * events: 1 for the rear camera, 2 for the front */
static unsigned int session_id = 1;
/* the front camera's command array length: 0 = empty (the kernel's own
 * tables are written), 1 = a dummy entry */
static int front_cmd_count = 0;
/* the CPP firmware file to load (0 = none) */
static const char *cpp_fw_path = 0;
/* the ISP's input source: CAMIF_RAW (2) feeds the ISP with the raw
 * Bayer from the CSID (and then the CPP), PIX_VIEWFINDER (1) is the
 * ISP's own output and bypasses the CPP */
static unsigned int isp_stream_src = CAMIF_RAW;

static unsigned rd32(const unsigned char *p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24);
}
static void wr32(unsigned char *p, unsigned v)
{
	p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

/* ------------------------------------------------------------------ *
 * the sensor
 * ------------------------------------------------------------------ */
static int sensor_fd = -1;

static int sensor_cfg(int cfgtype, void *setting, unsigned int size)
{
	struct sensorb_cfg_data c;
	memset(&c, 0, sizeof(c));
	c.cfgtype = cfgtype;
	c.setting_size = size;
	if (setting)
		/* the union member is a POINTER: the kernel copies the
		 * struct out of user space from there, so the address
		 * has to be handed over, not a copy of the struct */
		*(void **)c.cfg = setting;
	if (ioctl(sensor_fd, SENSOR_CFG_IOCTL, &c) < 0) {
		printf("[sensor] cfg %d: %s\n", cfgtype, strerror(errno));
		return -1;
	}
	return 0;
}

/*
 * Write the sensor's registers.
 *
 * The data type matters: the vendor's camera HAL writes the *geometry*
 * (0x0340, 0x0342) as one 16-bit word and everything else as bytes --
 * that is what its own ioctl headers say (the `sensorio_*.bin`
 * captures).  A byte write of the geometry puts the bytes the other way
 * round, and the sensor then runs in the wrong mode.
 *
 * A register with the high bit set in its address is written as a
 * word: 0x8000 | addr.
 */
static int sensor_write_regs_as(const struct imx179_reg *regs, int n,
			       unsigned int data_type)
{
	struct msm_camera_i2c_reg_setting set;
	struct msm_camera_i2c_reg_array *arr;
	int i, words = 0;

	arr = malloc(n * sizeof(*arr));
	if (!arr)
		return -1;
	for (i = 0; i < n; i++) {
		arr[i].reg_addr = regs[i].addr;
		arr[i].reg_data = regs[i].data;
		arr[i].delay = regs[i].delay;
	}
	memset(&set, 0, sizeof(set));
	set.reg_setting = arr;
	set.size = n;
	set.addr_type = MSM_CAMERA_I2C_WORD_ADDR;
	set.data_type = data_type;
	set.delay = 0;
	if (sensor_cfg(CFG_WRITE_I2C_ARRAY, &set, sizeof(set)) < 0) {
		free(arr);
		return -1;
	}
	free(arr);
	printf("[sensor] wrote %d registers\n", n);
	return 0;
}

/* the usual way: one byte per register */
static int sensor_write_regs(const struct imx179_reg *regs, int n)
{
	return sensor_write_regs_as(regs, n, MSM_CAMERA_I2C_BYTE_DATA);
}

/* --- reading the sensor's registers back --------------------------------
 *
 * The kernel's sensor-config ioctl can read a register
 * (CFG_SLAVE_READ_I2C), which is how the imaging server can ask the
 * sensor what mode it is really in instead of guessing from the table.
 */
struct msm_camera_i2c_read_config {
	unsigned short slave_addr;
	unsigned short reg_addr;
	unsigned int data_type;		/* enum msm_camera_i2c_data_type */
	unsigned short *data;
};

#define CFG_SLAVE_READ_I2C	1
#define MSM_CAMERA_I2C_BYTE_DATA	1
#define IMX179_SLAVE_ADDR	0x20

static int sensor_read_reg(uint16_t reg, uint16_t *val)
{
	struct msm_camera_i2c_read_config rc;
	uint16_t data = 0;

	memset(&rc, 0, sizeof(rc));
	rc.slave_addr = IMX179_SLAVE_ADDR;
	rc.reg_addr = reg;
	rc.data_type = MSM_CAMERA_I2C_BYTE_DATA;
	rc.data = &data;
	if (sensor_cfg(CFG_SLAVE_READ_I2C, &rc, sizeof(rc)) < 0)
		return -1;
	*val = data;
	return 0;
}

/* the registers that carry the mode and the frame geometry */
static const uint16_t geom_regs[] = {
	0x0100, 0x0101, 0x0102, 0x0105, 0x0106,
	0x0301, 0x0303, 0x0305, 0x0307, 0x0309, 0x030b, 0x030c, 0x030d,
	0x0340, 0x0341, 0x0342, 0x0343, 0x0344, 0x0345,
	0x034c, 0x034d, 0x034e, 0x034f, 0x0350, 0x0351,
};

static void sensor_dump_regs(const char *when)
{
	unsigned i;

	printf("[sensor] register read-back (%s):\n", when);
	for (i = 0; i < sizeof(geom_regs) / sizeof(geom_regs[0]); i++) {
		uint16_t v = 0;
		if (sensor_read_reg(geom_regs[i], &v) < 0) {
			printf("  0x%04x: <read failed>\n", geom_regs[i]);
			continue;
		}
		printf("  0x%04x = 0x%04x\n", geom_regs[i], v);
	}
	fflush(stdout);
}

/* the IMX179 starts streaming when 0x0100 is set to 1 */
static const struct imx179_reg stream_on_regs[] = { { 0x0100, 0x01, 0 } };
static const struct imx179_reg stream_off_regs[] = { { 0x0100, 0x00, 0 } };

/*
 * The vendor's own sequence, captured from the ROM's camera HAL
 * (sensor_config.h).
 *
 * The IMX179's mode change is not one register array: the HAL writes a
 * 92-entry "page" sequence first (the sensor's PLL, MIPI and analogue
 * settings, written through the page registers 0xfffd/0x0080/0xfffc),
 * then the 62-entry mode table, then a "config change" array, then the
 * geometry, and only then 0x0100 = 1 to start streaming.  Writing the
 * mode table alone leaves the sensor silent on the MIPI link.
 */
static int use_vendor_sensor_seq = 0;

/*
 * Program the sensor from the register list that was read out of the
 * sensor while the ROM's own camera app had it open (sensor_live.h),
 * instead of the arrays captured from an older camera stack.
 *
 *   1 = the vendor's live list only
 *   2 = the captured page sequence first, then the vendor's live list
 */
static int use_live_sensor_seq = 0;
static int use_vendor_sensor_seq = 0;
static int frames_to_wait = 10;

static int sensor_write_array(const struct sensor_write *w)
{
	struct msm_camera_i2c_reg_setting set;

	memset(&set, 0, sizeof(set));
	set.reg_setting = (struct msm_camera_i2c_reg_array *)w->regs;
	set.size = w->count;
	set.addr_type = MSM_CAMERA_I2C_WORD_ADDR;
	set.data_type = MSM_CAMERA_I2C_BYTE_DATA;
	return sensor_cfg(CFG_WRITE_I2C_ARRAY, &set, sizeof(set));
}

/*
 * The order in which the vendor's arrays are written.
 *
 * The capture holds every write the camera HAL made during a whole
 * session, and that session included several *mode changes*: the
 * captures 7 and 9 to 13 belong to other modes (their HMAX is 0xda,
 * 0xb3, 0x9e -- the mode the camera uses has 0xc8).  Writing them
 * all leaves the sensor in the last mode, with a 40k-pixel line,
 * which is not the mode the camera runs in -- and then the image's
 * packets do not arrive at all.
 *
 * The mode is: the page array, the mode table, the stream-off, the
 * config change, the geometry (the captures 0 to 6) and then the
 * stream-on (the capture 8).
 */
static const int vendor_sequence_order[] = { 0, 1, 2, 3, 4, 5, 6, 8 };
#define VENDOR_SEQUENCE_STEPS \
	(sizeof(vendor_sequence_order) / sizeof(vendor_sequence_order[0]))

static int sensor_write_vendor_sequence(void)
{
	unsigned i;

	for (i = 0; i < VENDOR_SEQUENCE_STEPS; i++) {
		int n = vendor_sequence_order[i];

		if (sensor_write_array(&sensor_sequence[n]) < 0) {
			printf("[sensor] vendor array %d (%d regs): failed\n",
			       n, sensor_sequence[n].count);
			return -1;
		}
	}
	printf("[sensor] vendor sequence: %d arrays written\n",
	       (int)VENDOR_SEQUENCE_STEPS);
	return 0;
}

/*
 * The vendor's live register list.
 *
 * The captures in sensor_config.h come from an older camera stack, and
 * they differ from what the sensor really holds in three ways that
 * matter: the PLL (0x0202/0x0203), the byte order of the geometry
 * (0x0340/0x0341 and 0x0342/0x0343), and three registers that the
 * captures do not have at all (0x0304, 0x0306, 0x0407).  With the
 * wrong values the sensor runs in the wrong mode and sends nothing but
 * the embedded data.
 */
static int sensor_write_live_sequence(void)
{
	if (use_live_sensor_seq == 2) {
		/* the page sequence: the analog settings, which live on
		 * the sensor's other pages */
		if (sensor_write_array(&sensor_sequence[0]) < 0) {
			printf("[sensor] the page sequence: failed\n");
			return -1;
		}
	}
	if (sensor_write_regs(imx179_live_photo, IMX179_LIVE_PHOTO_LEN) < 0) {
		printf("[sensor] the vendor's live list: failed\n");
		return -1;
	}
	/* the geometry is a word write, and the data type belongs to
	 * the whole array -- so it needs an array of its own */
	if (sensor_write_regs_as(imx179_live_geometry, IMX179_LIVE_GEOMETRY_LEN,
				MSM_CAMERA_I2C_WORD_DATA) < 0) {
		printf("[sensor] the geometry: failed\n");
		return -1;
	}
	printf("[sensor] the vendor's live list: %d registers written\n",
	       (int)IMX179_LIVE_PHOTO_LEN);
	return 0;
}

static int sensor_program_mode(void)
{
	if (sensor_cfg(CFG_POWER_UP, NULL, 0) < 0)
		return -1;
	printf("[sensor] powered up\n");
	if (cam->kernel_programs_sensor) {		/* The MT9M114B driver has the tables and writes them itself,
		 * but the kernel copies the user's array out first, so a
		 * (dummy) array has to be supplied. */
		static struct msm_camera_i2c_reg_array dummy[1];
		struct msm_camera_i2c_reg_setting set;
		int n = front_cmd_count;	/* 0 = empty array */
		memset(&set, 0, sizeof(set));
		set.reg_setting = n ? dummy : NULL;
		set.size = n;
		set.addr_type = MSM_CAMERA_I2C_WORD_ADDR;
		set.data_type = MSM_CAMERA_I2C_BYTE_DATA;
		sensor_cfg(CFG_SET_INIT_SETTING, &set, sizeof(set));
		sensor_cfg(CFG_SET_RESOLUTION, &set, sizeof(set));
		printf("[sensor] init + resolution set (by the kernel driver)\n");
		return 0;
	}
	if (use_live_sensor_seq)
		return sensor_write_live_sequence();
	if (use_vendor_sensor_seq) {
		/* the vendor's own sequence (page sequence, mode table,
		 * config change, geometry, and the stream-on) */
		if (sensor_write_vendor_sequence() < 0)
		if (poke_reg != 0xffffffff) {
			struct imx179_reg one;
			one.addr = poke_reg;
			one.data = (uint16_t)poke_val;
			one.delay = 0;
			sensor_write_regs(&one, 1);
			printf("[sensor] poked 0x%04x = 0x%04x\n", poke_reg,
			       poke_val);
		}
			return -1;
		return 0;
	}
	if (sensor_write_regs(IMX179_MODE_TABLE, IMX179_MODE_TABLE_LEN) < 0)
		return -1;
	if (cam->binned) {
		/* the binning mode is the main settings plus the
		 * "config change" settings of the 1632x1224 mode:
		 * HMAX 1640, VMAX 1232 */
		sensor_write_regs(imx179_settings_7, 20);
	}
	return 0;
}

/* ------------------------------------------------------------------ *
 * the MIPI link
 * ------------------------------------------------------------------ */
static int csiphy_fd = -1;
static int csid_fd = -1;

/* the virtual channel the sensor uses, and the MIPI data type */
static int cam_vc = 1;
/*
 * The data types the IMX179 sends, as the *vendor's* camera configures
 * them.  Measured by running the ROM's camera under our kernel, which
 * logs the CSID's own registers:
 *
 *   CSID0 LUT: vc0 0x0000122b vc1 0x00000000 ..., cfg0 0x00000023 ...
 *
 * The CSID's LUT holds one byte per channel: channel 0 (the one the
 * ISP-IF routes to PIX0) carries data type 0x2b with decode format 2,
 * and channel 1 carries 0x12 with decode format 1.  The CFG register
 * holds (decode_format << 4) | 3, which is why the vendor's cfg0 is
 * 0x23 and not 0x13.
 *
 * We had 0x12 on channel 0: the packets then stayed unmapped (the
 * CSID's "captured unmapped long packet header" read 0x2b010405 -- the
 * data type 0x2b the sensor really sends) and the ISP delivered an
 * empty frame.
 */
static int cam_dt = 0x2b;	/* the data type on the routed channel */
static int cam_dt_other = 0x12;	/* and the one on the other channel */
static int lut_cid0 = 0;	/* the CSID's LUT: which VC each entry maps */
static int lut_cid1 = 1;

static int mipi_config(void)
{
	struct csiphy_cfg_data cphy;
	struct msm_camera_csiphy_params cphy_params;
	struct csid_cfg_data cid;
	struct msm_sensor_csid_cfg_params cid_params;
	struct msm_camera_csid_params cparams;
	struct msm_camera_csid_lut_params lut;
	struct msm_camera_csid_vc_cfg vc;
	struct msm_camera_csid_vc_cfg vc1;

	csiphy_fd = open(cam->csiphy_node, O_RDWR);
	csid_fd = open(cam->csid_node, O_RDWR);
	if (csiphy_fd < 0 || csid_fd < 0) {
		printf("[mipi] open %s/%s: %s\n", cam->csiphy_node,
		       cam->csid_node, strerror(errno));
		return -1;
	}

	/* CSIPHY_INIT maps the PHY's registers */
	memset(&cphy, 0, sizeof(cphy));
	cphy.cfgtype = CSIPHY_INIT;
	if (ioctl(csiphy_fd, VIDIOC_MSM_CSIPHY_IO_CFG, &cphy) < 0)
		printf("[mipi] CSIPHY_INIT: %s\n", strerror(errno));

	memset(&cphy_params, 0, sizeof(cphy_params));
	cphy_params.lane_cnt = CAM_LANE_CNT;
	cphy_params.settle_cnt = 14;
	cphy_params.lane_mask = CAM_LANE_MASK;
	cphy_params.combo_mode = 0;
	cphy_params.csid_core = cam->csid_core;
	memset(&cphy, 0, sizeof(cphy));
	cphy.cfgtype = CSIPHY_CFG;
	cphy.cfg.csiphy_params = &cphy_params;
	cphy.cfg_params_size = sizeof(cphy_params);
	if (ioctl(csiphy_fd, VIDIOC_MSM_CSIPHY_IO_CFG, &cphy) < 0)
		printf("[mipi] CSIPHY_CFG: %s\n", strerror(errno));
	else
		printf("[mipi] CSIPHY configured: %d lanes, mask 0x%x, csid %d\n",
		       cphy_params.lane_cnt, cphy_params.lane_mask, cphy_params.csid_core);

	/* CSID_INIT likewise */
	memset(&cid, 0, sizeof(cid));
	cid.cfgtype = CSID_INIT;
	if (ioctl(csid_fd, VIDIOC_MSM_CSID_IO_CFG, &cid) < 0)
		printf("[mipi] CSID_INIT: %s\n", strerror(errno));

	memset(&vc, 0, sizeof(vc));
	vc.cid = lut_cid0;		/* the channel the ISP-IF routes */
	vc.dt = cam_dt;
	vc.decode_format = 2;
	memset(&vc1, 0, sizeof(vc1));
	vc1.cid = lut_cid1;		/* the other channel */
	vc1.dt = cam_dt_other;
	vc1.decode_format = 1;
	memset(&lut, 0, sizeof(lut));
	/* The CSID's LUT is indexed by the virtual channel the sensor
	 * uses; the vendor's camera programs both of them. */
	lut.num_cid = 2;
	lut.vc_cfg[0] = &vc;
	lut.vc_cfg[1] = &vc1;
	lut.vc_cfg_size = sizeof(vc);
	memset(&cparams, 0, sizeof(cparams));
	cparams.lane_cnt = CAM_LANE_CNT;
	cparams.lane_assign = CAM_LANE_ASSIGN;
	cparams.phy_sel = 0;
	cparams.lut_params = lut;
	memset(&cid_params, 0, sizeof(cid_params));
	cid_params.csid_params = &cparams;
	cid_params.csid_params_size = sizeof(cparams);
	memset(&cid, 0, sizeof(cid));
	cid.cfgtype = CSID_CFG;
	cid.cfg.csid_cfg_params = cid_params;
	if (ioctl(csid_fd, VIDIOC_MSM_CSID_IO_CFG, &cid) < 0)
		printf("[mipi] CSID_CFG: %s\n", strerror(errno));
	else
		printf("[mipi] CSID configured: %d lanes, assign 0x%x\n",
		       cparams.lane_cnt, cparams.lane_assign);
	return 0;
}

/* ------------------------------------------------------------------ *
 * the ISP
 * ------------------------------------------------------------------ */
static int isp_fd = -1;

/* the ISP's buffer manager handle and the AXI stream handle: the
 * camera2 HAL uses them in every per-frame ioctl. */
static unsigned int isp_buf_handle;
static unsigned int isp_stream_handle;
/*
 * There are two VFE subdevices on this device: the 8974's (vfe32) and
 * a vfe40.  Which one the rear camera's CSID feeds decides where the
 * frames come out, and the subdevice number is the only way to tell
 * them apart at run time, so it can be chosen.
 */
static int isp_vfe = 0;		/* which subdev: 0 = 13, 1 = 14 */
static unsigned int pix_clk_override;	/* the CAMIF's pixel clock */
static int ispif_vfe = 0;		/* which VFE the ISP-IF routes into */

static int isp_open(void)
{
	static const char *cands[] = {
		"/dev/v4l-subdev13", "/dev/v4l-subdev14", NULL
	};
	int i;

	if (isp_fd >= 0)
		return isp_fd;
	for (i = 0; cands[i]; i++) {
		if ((isp_vfe == 0) != (i == 0))
			continue;
		int fd = open(cands[i], O_RDWR);
		if (fd < 0)
			continue;
		printf("[isp] %s open\n", cands[i]);
		isp_fd = fd;
		return isp_fd;
	}
	printf("[isp] no vfe subdevice: %s\n", strerror(errno));
	return -1;
}

/* the client's buffers, so that the server can hand them to the ISP */
struct client_buffers {
	volatile int	count;
	int		width;
	int		height;
	struct v4l2_buffer	buf[8];
	struct v4l2_plane	planes[8][3];
};
static struct client_buffers cbufs;
static int client_buf_fd[8];

/* latest QVGA frame slot */
static pthread_mutex_t frame_mutex = PTHREAD_MUTEX_INITIALIZER;
static unsigned char *latest_y = NULL;
static unsigned char *latest_uv = NULL;
static int latest_seq = 0;
static int latest_ready = 0;

/* ------------------------------------------------------------------ *
 * the ISP's configuration: the chromatrix and the rest of the camera2
 * "cam_config", captured from the vendor HAL (cam_config.h)
 * ------------------------------------------------------------------ */
/*
 * The camera2 stack hands the ISP its configuration with
 * VIDIOC_MSM_VFE_REG_CFG: a list of struct msm_vfe_reg_cfg_cmd blocks
 * (register writes, mask writes and DMI table uploads) plus one data
 * blob.  The vendor HAL sends it once when the camera is configured and
 * then again for every frame; the bytes are in cam_config.h, captured
 * from the vendor HAL itself, so nothing of the vendor's is needed at
 * run time.
 */
static int replay_config = 1;	/* the initial configuration */
static int replay_cycle = 1;	/* and the per-frame cycle */

static int isp_replay(const struct cam_upload *u, int n, const char *what)
{
	struct msm_vfe_cfg_cmd2 cmd;
	int i, bad = 0;

	if (isp_open() < 0)
		return -1;
	for (i = 0; i < n; i++) {
		memset(&cmd, 0, sizeof(cmd));
		cmd.num_cfg = u[i].num_cfg;
		cmd.cmd_len = u[i].cmd_len;
		cmd.cfg_cmd = (void *)u[i].blocks;
		cmd.cfg_data = (void *)u[i].data;
		if (ioctl(isp_fd, VIDIOC_MSM_VFE_REG_CFG, &cmd) < 0) {
			printf("[isp] REG_CFG %d (%u blocks, %u bytes): %s\n",
			       i, u[i].num_cfg, u[i].cmd_len, strerror(errno));
			bad++;
		}
	}
	printf("[isp] %s: %d uploads sent, %d failed\n", what, n, bad);
	fflush(stdout);
	return bad ? -1 : 0;
}

/* ------------------------------------------------------------------ *
 * the ISP-IF: the piece between the CSID and the ISP
 * ------------------------------------------------------------------ */
/*
 * The camera2 stack tells the ISP-IF how the two are connected -- which
 * CSID, which virtual channel, which VFE interface -- and then starts
 * it.  Without this the CSID receives the sensor's packets and drops
 * them: we measured 11,808 of them, with no ECC and no CRC errors,
 * and no frame ever came out.
 *
 * The values here are the ones the HAL uses, and the ones the device
 * itself shows: the IMX179 sends its packets on **virtual channel 1**
 * (the CSID's "unmapped long packet header" is 0x1201040a, whose VC
 * field is 1), and the rear camera is on **CSID 0** (the device tree
 * says qcom,csid-sd-index = <0>).
 */
static int ispif_fd = -1;
static int cam_cid = CID0;	/* the virtual channel (the vendor routes cid 0) */
static int cam_csid = CSID0;	/* the rear camera's CSID */

static int ispif_ioctl(struct ispif_cfg_data *cfg, const char *what)
{
	if (ioctl(ispif_fd, VIDIOC_MSM_ISPIF_CFG, cfg) < 0) {
		printf("[ispif] %s: %s\n", what, strerror(errno));
		return -1;
	}
	printf("[ispif] %s ok\n", what);
	return 0;
}

static int ispif_config(void)
{
	struct ispif_cfg_data cfg;

	if (ispif_fd < 0) {
		ispif_fd = open(ISPIF_NODE, O_RDWR);
		if (ispif_fd < 0) {
			printf("[ispif] open %s: %s\n", ISPIF_NODE,
			       strerror(errno));
			return -1;
		}
		printf("[ispif] %s open\n", ISPIF_NODE);
	}

	/* the VFE's information */
	memset(&cfg, 0, sizeof(cfg));
	cfg.cfg_type = ISPIF_SET_VFE_INFO;
	cfg.vfe_info.num_vfe = 1;
	cfg.vfe_info.info[0].max_resolution = 3264 * 2448;
	cfg.vfe_info.info[0].id = 0;
	cfg.vfe_info.info[0].ver = 0;
	if (ispif_ioctl(&cfg, "SET_VFE_INFO") < 0)
		return -1;

	/* the CSID's hardware version */
	memset(&cfg, 0, sizeof(cfg));
	cfg.cfg_type = ISPIF_INIT;
	cfg.csid_version = CSID_VERSION_V30;
	if (ispif_ioctl(&cfg, "INIT") < 0)
		return -1;

	/* the routing */
	memset(&cfg, 0, sizeof(cfg));
	cfg.cfg_type = ISPIF_CFG;
	cfg.params.num = 1;
	cfg.params.entries[0].vfe_intf = ispif_vfe ? VFE1 : VFE0;
	cfg.params.entries[0].intftype = PIX0;
	/*
	 * The channels the interface takes.  The vendor asks for one
	 * (cid 0), but the packets arrive on cid 1 under our kernel, and
	 * the ISP-IF takes a MASK of channels -- so it can be given
	 * both, and the one that carries the image is the one the CSID
	 * maps the image's data type to.
	 *
	 * (Giving it cid 1 alone crashes the kernel: the path for a
	 * single non-zero cid is not exercised anywhere else.)
	 */
	if (two_cids) {
		cfg.params.entries[0].num_cids = 2;
		cfg.params.entries[0].cids[0] = 0;
		cfg.params.entries[0].cids[1] = 1;
	} else {
		cfg.params.entries[0].num_cids = 1;
		cfg.params.entries[0].cids[0] = cam_cid;	/* the vendor routes cid 0 */
	}
	cfg.params.entries[0].csid = cam_csid;
	/*
	 * The crop has to be enabled: msm_ispif_enable_crop() -- the
	 * function that sets the PIX0 enable bit in CTRL_0 -- is only
	 * called when crop_enable is set, so without it the ISP's
	 * pixel input is never switched on and no data ever arrives
	 * (measured: CTRL_0 stayed 0x40, the enable bit 0x80 missing).
	 * The window is the same one the ISP's own CAMIF uses.
	 */
	/*
	 * The crop has to be enabled.  msm_ispif_enable_crop() -- the
	 * function that sets the PIX0 enable bit in CTRL_0 -- is only
	 * called when crop_enable is set (msm_ispif.c, the config
	 * handler), so with crop_enable 0 the ISP-IF's pixel path is
	 * never switched on and no data ever arrives: the frame comes
	 * back with bytesused 0.
	 *
	 * The window is the CAMIF's own: the first pixel and the last
	 * pixel of the line the sensor sends.
	 */
	cfg.params.entries[0].crop_enable = crop_enable;
	cfg.params.entries[0].crop_start_pixel = 0;
	cfg.params.entries[0].crop_end_pixel =
		cam->binned ? 1631 : cam->width - 1;	/* 1631 / 3279 */
	if (ispif_ioctl(&cfg, "CFG") < 0)
		return -1;

	/* go */
	memset(&cfg, 0, sizeof(cfg));
	cfg.cfg_type = ISPIF_START_FRAME_BOUNDARY;
	cfg.params.num = 1;
	cfg.params.entries[0].vfe_intf = ispif_vfe ? VFE1 : VFE0;
	cfg.params.entries[0].intftype = PIX0;
	cfg.params.entries[0].num_cids = 1;
	cfg.params.entries[0].cids[0] = cam_cid;	/* the vendor routes cid 0 */
	cfg.params.entries[0].csid = cam_csid;
	/*
	 * The crop has to be enabled.  msm_ispif_enable_crop() -- the
	 * function that sets the PIX0 enable bit in CTRL_0 -- is only
	 * called when crop_enable is set (msm_ispif.c, the config
	 * handler), so with crop_enable 0 the ISP-IF's pixel path is
	 * never switched on and no data ever arrives: the frame comes
	 * back with bytesused 0.
	 *
	 * The window is the CAMIF's own: the first pixel and the last
	 * pixel of the line the sensor sends.
	 */
	cfg.params.entries[0].crop_enable = crop_enable;
	cfg.params.entries[0].crop_start_pixel = 0;
	cfg.params.entries[0].crop_end_pixel =
		cam->binned ? 1631 : cam->width - 1;	/* 1631 / 3279 */
	if (ispif_ioctl(&cfg, "START_FRAME_BOUNDARY") < 0)
		return -1;

	return 0;
}

/* The CPP's node, kept open for the whole run: the chromatrix has to
 * be handed to the CPP once per frame (the camera2 HAL does exactly
 * that -- the kernel logs 256 CPP CFG ioctls for 256 frames), because
 * the kernel patches the frame's current buffer addresses into the
 * frame message when it accepts it, and the buffers rotate. */
static int cpp_fd = -1;

/* Hand the chromatrix to the CPP.  Returns 0 on success. */
static int cpp_send_chromatrix(void)
{
	struct msm_camera_v4l2_ioctl_t io;
	struct msm_cpp_frame_info_t frame;
	int32_t cpp_status = 0;

	if (cpp_fd < 0)
		return -1;

	memset(&frame, 0, sizeof(frame));
	frame.frame_type = 1;		/* realtime */
	frame.msg_len = CAM_CPP_MSG_WORDS;
	frame.cpp_cmd_msg = cam_cpp_msg;
	frame.status = &cpp_status;
	frame.identity = ((uint32_t)session_id << 16) | (uint32_t)0;

	memset(&io, 0, sizeof(io));
	io.id = session_id;
	io.len = sizeof(frame);
	io.trans_code = 0;
	io.ioctl_ptr = &frame;
	if (ioctl(cpp_fd, VIDIOC_MSM_CPP_CFG, &io) < 0) {
		printf("[cpp] CFG: %s\n", strerror(errno));
		return -1;
	}
	printf("[cpp] chromatrix sent (%d words, status %d)\n",
	       frame.msg_len, cpp_status);
	return 0;
}

static int isp_start(void)
{
	struct msm_isp_buf_request breq;
	struct msm_vfe_axi_stream_request_cmd scmd;
	struct msm_vfe_axi_stream_cfg_cmd ccmd;
	struct msm_vfe_input_cfg icfg;
	int i;

	if (isp_open() < 0)
		return -1;
	if (!cbufs.count) {
		printf("[isp] no client buffers yet\n");
		return -1;
	}

	memset(&breq, 0, sizeof(breq));
	breq.session_id = session_id;
	breq.stream_id = 0;
	breq.num_buf = cbufs.count;
	breq.handle = 0;
	breq.buf_type = ISP_SHARE_BUF;
	if (ioctl(isp_fd, VIDIOC_MSM_ISP_REQUEST_BUF, &breq) < 0) {
		printf("[isp] REQUEST_BUF: %s\n", strerror(errno));
		return -1;
	}
	isp_buf_handle = breq.handle;
	printf("[isp] REQUEST_BUF ok, handle=%u\n", breq.handle);

	memset(&icfg, 0, sizeof(icfg));
	icfg.input_src = VFE_PIX_0;
	icfg.input_pix_clk = pix_clk_override ? pix_clk_override :
							    cam->pix_clk;
	icfg.d.pix_cfg.input_mux = CAMIF;
	icfg.d.pix_cfg.camif_cfg.camif_input = 3;	/* the vendor's value */
	icfg.d.pix_cfg.camif_cfg.lines_per_frame = cam->height;
	icfg.d.pix_cfg.camif_cfg.pixels_per_line = cam->width;
	icfg.d.pix_cfg.camif_cfg.first_pixel = 0;
	/*
	 * The window: the camera front end rejects a frame whose geometry
	 * does not match exactly ("camif violation"), and the vendor's
	 * own values are the full line and the full frame -- not the
	 * active area.  These are the values the ROM's camera HAL sends.
	 */
	icfg.d.pix_cfg.camif_cfg.last_pixel =
		cam->binned ? 1631 : cam->width - 1;	/* 1631 / 3279 */
	icfg.d.pix_cfg.camif_cfg.last_line =
		cam->binned ? 1231 : cam->height - 1;	/* 1231 / 2463 */
	icfg.d.pix_cfg.camif_cfg.first_line = 0;
	icfg.d.pix_cfg.camif_cfg.last_line =
		cam->binned ? 1231 : cam->height - 1;	/* 1231 / 2463 */
	icfg.d.pix_cfg.camif_cfg.epoch_line0 = 0;
	icfg.d.pix_cfg.camif_cfg.epoch_line1 = cam->height;
	if (ioctl(isp_fd, VIDIOC_MSM_ISP_INPUT_CFG, &icfg) < 0)
		printf("[isp] INPUT_CFG: %s\n", strerror(errno));
	else
		printf("[isp] input configured: CAMIF %dx%d\n",
		       cam->width, cam->height);

	memset(&scmd, 0, sizeof(scmd));
	scmd.session_id = session_id;
	scmd.stream_id = 0;
	scmd.output_format = ISP_PIX_FMT_NV12;
	scmd.stream_src = isp_stream_src;
	scmd.init_frame_drop = 0;
	scmd.frame_skip_pattern = NO_SKIP;
	scmd.plane_cfg[0].output_width = cbufs.width;
	scmd.plane_cfg[0].output_height = cbufs.height;
	scmd.plane_cfg[0].output_stride = cbufs.width;
	scmd.plane_cfg[0].output_scan_lines = cbufs.height;
	scmd.plane_cfg[0].output_plane_format = Y_PLANE;
	scmd.plane_cfg[1].output_width = cbufs.width;
	scmd.plane_cfg[1].output_height = cbufs.height;
	scmd.plane_cfg[1].output_stride = cbufs.width;
	scmd.plane_cfg[1].output_scan_lines = cbufs.height;
	scmd.plane_cfg[1].output_plane_format = CBCR_PLANE;
	if (ioctl(isp_fd, VIDIOC_MSM_ISP_REQUEST_STREAM, &scmd) < 0) {
		printf("[isp] REQUEST_STREAM: %s\n", strerror(errno));
		return -1;
	}
	isp_stream_handle = scmd.axi_stream_handle;
	printf("[isp] REQUEST_STREAM ok, handle=%u\n", scmd.axi_stream_handle);


	for (i = 0; i < cbufs.count; i++) {
		struct msm_isp_qbuf_info q;
		memset(&q, 0, sizeof(q));
		q.handle = breq.handle;
		q.buf_idx = i;
		q.buffer = cbufs.buf[i];
		if (ioctl(isp_fd, VIDIOC_MSM_ISP_ENQUEUE_BUF, &q) < 0) {
			printf("[isp] ENQUEUE_BUF %d: %s\n", i, strerror(errno));
			return -1;
		}
	}
	printf("[isp] %d buffers enqueued\n", cbufs.count);

	memset(&ccmd, 0, sizeof(ccmd));
	ccmd.num_streams = 1;
	ccmd.stream_handle[0] = scmd.axi_stream_handle;
	ccmd.cmd = START_STREAM;
	if (ioctl(isp_fd, VIDIOC_MSM_ISP_CFG_STREAM, &ccmd) < 0) {
		printf("[isp] CFG_STREAM: %s\n", strerror(errno));
		return -1;
	}
	printf("[isp] stream started\n");
	if (replay_config)
		isp_replay(cam_initial, CAM_INITIAL_COUNT,
			   "initial configuration");
	if (send_capture_config)
		isp_replay(cam_capture, CAM_CAPTURE_COUNT,
			   "capture configuration");
	/* The ISP-IF comes LAST: the ISP's CFG_STREAM(START) resets the
	 * VFE's registers, so anything written before it is wiped --
	 * that is why the PIX0 enable bit (CTRL_0 bit 7) was missing. */
	if (replay_config)
		ispif_config();

	/* The CPP: it sits between the ISP and memory, and the camera2
	 * HAL hands it the chromatrix as a frame message.  Without it
	 * the ISP's output never leaves the CPP. */
	if (cpp_fw_path && strcmp(cpp_fw_path, "-") != 0 &&
	    isp_stream_src == CAMIF_RAW) {
		struct msm_camera_v4l2_ioctl_t io;
		struct msm_cpp_frame_info_t frame;
		int32_t cpp_status = 0;
		int fd = open(CPP_NODE, O_RDWR);

		if (fd < 0) {
			printf("[cpp] open %s: %s\n", CPP_NODE,
			       strerror(errno));
		} else {
			cpp_fd = fd;
			memset(&frame, 0, sizeof(frame));
			frame.frame_type = 1;		/* realtime */
			frame.msg_len = CAM_CPP_MSG_WORDS;
			frame.cpp_cmd_msg = cam_cpp_msg;
			/* The CPP writes the result of the frame
			 * through this pointer.  Left NULL the copy
			 * back fails and the whole ioctl is reported
			 * as an error, even though the chromatrix
			 * was accepted. */
			frame.status = &cpp_status;
			/* The CPP looks its buffer queue up by the
			 * identity: the session in the top half, the
			 * stream in the bottom.  Without it the queue
			 * is not found and the chromatrix is
			 * rejected. */
			frame.identity = ((uint32_t)session_id << 16) |
				(uint32_t)0;	/* the stream */

			/* The CPP needs its buffer queue before it
			 * will take the chromatrix: the queue is
			 * what gives it the physical address of the
			 * buffer the ISP writes into.  ENQUEUE (not
			 * APPEND) is what *creates* the queue
			 * entry; APPEND only adds buffers to an
			 * existing one. */
			{
				struct msm_cpp_stream_buff_info_t sbi;
				struct msm_cpp_buffer_info_t bi[8];
				int nbufs = cbufs.count > 0 ? cbufs.count : 1;
				int i;

				if (nbufs > (int)(sizeof(bi) / sizeof(bi[0])))
					nbufs = (int)(sizeof(bi) / sizeof(bi[0]));
				memset(bi, 0, sizeof(bi));
				memset(&sbi, 0, sizeof(sbi));
				sbi.identity = frame.identity;
				sbi.num_buffs = nbufs;
				sbi.buffer_info = bi;
				/* One entry per buffer, each with the index
				 * the ISP knows it by (the index of the
				 * vb2 buffer it was queued as) and the
				 * file descriptor of the memory
				 * behind it.  The kernel copies
				 * num_buffs of them, so the array
				 * has to be that long -- with one
				 * entry it reads past the end of it
				 * and queues stack garbage. */
				for (i = 0; i < nbufs; i++) {
					bi[i].fd = cbufs.count > 0 ?
						client_buf_fd[i] : -1;
					bi[i].index = i;
					bi[i].offset = 0;
					bi[i].native_buff = 0;
					bi[i].processed_divert = 0;
				}
				memset(&io, 0, sizeof(io));
				io.id = session_id;
				io.len = sizeof(sbi);
				io.trans_code = 0;
				io.ioctl_ptr = &sbi;
				if (ioctl(fd,
					VIDIOC_MSM_CPP_ENQUEUE_STREAM_BUFF_INFO,
					&io) < 0)
					printf("[cpp] ENQUEUE_STREAM_BUFF_INFO: %s\n",
					       strerror(errno));
				else
					printf("[cpp] buffer queue: %d buffers, fd %d\n",
					       sbi.num_buffs, bi[0].fd);
			}

			memset(&io, 0, sizeof(io));
			io.id = session_id;		/* the session! */
			io.len = sizeof(frame);
			io.trans_code = 0;		/* the stream id */
			io.ioctl_ptr = &frame;
			if (ioctl(fd, VIDIOC_MSM_CPP_CFG, &io) < 0)
				printf("[cpp] CFG: %s\n", strerror(errno));
			else
				printf("[cpp] chromatrix sent (%d words, status %d)\n",
				       frame.msg_len, cpp_status);
			/* The node stays open: the chromatrix has to be
			 * sent again for every frame (the kernel
			 * patches the frame's buffer addresses into
			 * the message when it takes it). */
		}
	}

	/* Tell the ISP that the pixel source is live, and that the
	 * configuration is complete: without these two it never
	 * processes a frame (the camera2 HAL sends both). */
	{
		struct msm_vfe_axi_src_state src;
		int done = 0;

		memset(&src, 0, sizeof(src));
		src.input_src = VFE_PIX_0;
		src.src_active = 1;
		if (ioctl(isp_fd, VIDIOC_MSM_ISP_SET_SRC_STATE, &src) < 0)
			printf("[isp] SET_SRC_STATE: %s\n", strerror(errno));
		else
			printf("[isp] SET_SRC_STATE ok\n");
		if (ioctl(isp_fd, VIDIOC_MSM_ISP_CONFIG_DONE, &done) < 0)
			printf("[isp] CONFIG_DONE: %s\n", strerror(errno));
		else
			printf("[isp] CONFIG_DONE ok\n");
	}
	return 0;
}


/* ------------------------------------------------------------------ *
 * the CPP: the kernel loads its firmware from a file
 * ------------------------------------------------------------------ */
/*
 * msm_cpp.c's cpp_load_fw() calls request_firmware() with the name the
 * user gave it, and the kernel's own loader then looks for that name in
 * /lib/firmware (and the other built-in paths).  On the ROM the file
 * lives in /vendor/firmware, which ueventd (the userspace firmware
 * class) serves; here we put it into /lib/firmware ourselves and let
 * the kernel pick it up.
 */
static int cpp_load_firmware(const char *path)
{
	char name[64];
	const char *base;
	char buf[16384];
	int fd, out, n;

	base = strrchr(path, '/');
	base = base ? base + 1 : path;
	snprintf(name, sizeof(name), "%s", base);

	/* copy it where the kernel's firmware loader will look */
	mkdir("/lib", 0755);
	mkdir("/lib/firmware", 0755);
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		printf("[cpp] open %s: %s\n", path, strerror(errno));
		return -1;
	}
	{
		char dst[128];
		snprintf(dst, sizeof(dst), "/lib/firmware/%s", name);
		out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (out < 0) {
			printf("[cpp] open %s: %s\n", dst, strerror(errno));
			close(fd);
			return -1;
		}
	}
	while ((n = read(fd, buf, sizeof(buf))) > 0)
		if (write(out, buf, n) != n) {
			printf("[cpp] write failed\n");
			break;
		}
	close(fd);
	close(out);
	printf("[cpp] %s staged in /lib/firmware\n", name);

	/* ask the CPP to load it */
	fd = open(CPP_NODE, O_RDWR);
	if (fd < 0) {
		printf("[cpp] open %s: %s\n", CPP_NODE, strerror(errno));
		return -1;
	}
	{
		struct msm_camera_v4l2_ioctl_t io;
		memset(&io, 0, sizeof(io));
		io.id = 0;
		io.len = strlen(name) + 1;
		io.trans_code = 0;
		io.ioctl_ptr = name;
		if (ioctl(fd, VIDIOC_MSM_CPP_LOAD_FIRMWARE, &io) < 0) {
			printf("[cpp] LOAD_FIRMWARE: %s\n", strerror(errno));
			close(fd);
			return -1;
		}
	}
	close(fd);
	printf("[cpp] firmware loaded\n");
	return 0;
}

/* ------------------------------------------------------------------ *
 * the server thread
 * ------------------------------------------------------------------ */
static volatile int server_ready;

static void *server_thread(void *arg)
{
	unsigned char buf[EVSZ];
	int fd = open(CONFIG_NODE, O_RDWR | O_NONBLOCK);
	unsigned id;

	if (fd < 0) {
		printf("[server] open %s: %s\n", CONFIG_NODE, strerror(errno));
		return NULL;
	}
	for (id = MSM_CAMERA_NEW_SESSION; id <= MSM_CAMERA_MSM_NOTIFY; id++) {
		struct v4l2_event_subscription sub;
		memset(&sub, 0, sizeof(sub));
		sub.type = MSM_CAMERA_V4L2_EVENT_TYPE;
		sub.id = id;
		if (ioctl(fd, VIDIOC_SUBSCRIBE_EVENT, &sub) < 0)
			printf("[server] SUBSCRIBE id=%u: %s\n", id, strerror(errno));
	}
	printf("[server] subscribed to event ids 1..7 -- we are the imaging server\n");
	fflush(stdout);
	server_ready = 1;

	for (;;) {
		struct pollfd pfd = { .fd = fd, .events = POLLIN };
		unsigned cmd;

		if (poll(&pfd, 1, 500) <= 0)
			continue;
		memset(buf, 0, sizeof(buf));
		if (ioctl(fd, DQ_IOCTL, buf) < 0) {
			if (errno != EAGAIN && errno != EINTR)
				printf("[server] DQEVENT: %s\n", strerror(errno));
			continue;
		}
		cmd = rd32(buf + OFF_CMD);
		session_id = rd32(buf + OFF_SESSION);
		printf("[server] event id=%u cmd=0x%08x session=%u stream=%u\n",
		       rd32(buf + OFF_ID), cmd, rd32(buf + OFF_SESSION),
		       rd32(buf + OFF_STREAM));
		fflush(stdout);

		switch (cmd) {
		case PRIV_S_FMT:
			mipi_config();
			sensor_program_mode();
			sensor_dump_regs("after programming");
			break;
		case PRIV_STREAM_ON:
			if (cpp_fw_path && strcmp(cpp_fw_path, "-") != 0 &&
			    isp_stream_src == CAMIF_RAW)
				cpp_load_firmware(cpp_fw_path);
			/*
			 * The ISP comes first, the sensor second.
			 *
			 * The order matters.  The IMX179 in the photo mode sends one
			 * frame and then stops, so a stream-on that comes before the
			 * pipeline is ready spends the only frame the sensor has.
			 * The vendor's camera sends its ISP configuration first and
			 * starts the sensor afterwards.
			 */
			isp_start();
			if (cam->kernel_programs_sensor) {
				static struct msm_camera_i2c_reg_array dummy[1];
				struct msm_camera_i2c_reg_setting set;
				memset(&set, 0, sizeof(set));
				set.reg_setting = front_cmd_count ? dummy : NULL;
				set.size = front_cmd_count;
				set.addr_type = MSM_CAMERA_I2C_WORD_ADDR;
				set.data_type = MSM_CAMERA_I2C_BYTE_DATA;
				sensor_cfg(CFG_SET_START_STREAM, &set, sizeof(set));
			}
			else
				sensor_write_regs(stream_on_regs, 1);
			sensor_dump_regs("after stream-on");
			break;
		case PRIV_STREAM_OFF:
			if (cam->kernel_programs_sensor)
				sensor_cfg(CFG_SET_STOP_STREAM, NULL, 0);
			else
				sensor_write_regs(stream_off_regs, 1);
			break;
		default:
			break;
		}

		wr32(buf + OFF_STATUS, MSM_CAMERA_CMD_SUCESS);
		wr32(buf + OFF_RET, 0);
		if (ioctl(fd, ACK_IOCTL, buf) < 0)
			printf("[server]   ACK failed: %s\n", strerror(errno));
		fflush(stdout);
	}
	return NULL;
}

/* ------------------------------------------------------------------ *
 * the capture client
 * ------------------------------------------------------------------ */
static int ion_alloc_fd(size_t len, unsigned int heap_mask)
{
	struct ion_allocation_data ad;
	struct ion_fd_data fd;
	int ion, r;

	ion = open("/dev/ion", O_RDWR);
	if (ion < 0) {
		printf("[ion] open /dev/ion: %s\n", strerror(errno));
		return -1;
	}
	memset(&ad, 0, sizeof(ad));
	ad.len = len;
	ad.align = 4096;
	ad.heap_id_mask = heap_mask;
	ad.flags = 0;
	if (ioctl(ion, ION_IOC_ALLOC, &ad) < 0) {
		printf("[ion] ION_IOC_ALLOC (mask 0x%x): %s\n", heap_mask,
		       strerror(errno));
		close(ion);
		return -1;
	}
	memset(&fd, 0, sizeof(fd));
	fd.handle = ad.handle;
	fd.fd = -1;
	if (ioctl(ion, ION_IOC_SHARE, &fd) < 0) {
		printf("[ion] ION_IOC_SHARE: %s\n", strerror(errno));
		close(ion);
		return -1;
	}
	printf("[ion] allocated %zu bytes, dma-buf fd %d\n", len, fd.fd);
	r = fd.fd;
	close(ion);
	return r;
}

static void *client_thread(void *arg)
{
	int *wh = (int *)arg;
	int width = wh[0], height = wh[1];
	unsigned char *bufs[8];
	struct v4l2_format fmt;
	struct v4l2_requestbuffers rb;
	struct v4l2_plane planes[3];
	int fd, i, n;

	while (!server_ready) {
		struct timespec ts = { 0, 20 * 1000 * 1000 };
		nanosleep(&ts, NULL);
	}
	fd = open(cam->sensor_node, O_RDWR);
	if (fd < 0) {
		printf("[client] open %s: %s\n", cam->sensor_node, strerror(errno));
		return NULL;
	}
	printf("[client] open %s SUCCEEDED -- the handshake completed\n",
	       cam->sensor_node);
	fflush(stdout);

	{
		struct v4l2_streamparm parm;
		memset(&parm, 0, sizeof(parm));
		parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		if (ioctl(fd, VIDIOC_S_PARM, &parm) == 0)
			printf("[client] S_PARM ok: extendedmode=%u\n",
			       parm.parm.capture.extendedmode);
		else
			printf("[client] S_PARM: %s\n", strerror(errno));
		fflush(stdout);
	}

	memset(&fmt, 0, sizeof(fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	{
		struct msm_v4l2_format_data fdata;
		memset(&fdata, 0, sizeof(fdata));
		fdata.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		fdata.width = width;
		fdata.height = height;
		fdata.pixelformat = V4L2_PIX_FMT_NV12;
		fdata.num_planes = 2;
		fdata.plane_sizes[0] = width * height;
		fdata.plane_sizes[1] = (width * height) / 2;
		memcpy(fmt.fmt.raw_data, &fdata, sizeof(fdata));
	}
	if (ioctl(fd, VIDIOC_S_FMT, &fmt) == 0)
		printf("[client] S_FMT ok: %ux%u\n", width, height);
	else
		printf("[client] S_FMT: %s\n", strerror(errno));
	fflush(stdout);

	memset(&rb, 0, sizeof(rb));
	rb.count = 4;
	rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	rb.memory = V4L2_MEMORY_USERPTR;
	if (ioctl(fd, VIDIOC_REQBUFS, &rb) < 0) {
		printf("[client] REQBUFS: %s\n", strerror(errno));
		return NULL;
	}
	printf("[client] REQBUFS ok: %u buffers\n", rb.count);
	fflush(stdout);

	for (i = 0; i < (int)rb.count; i++) {
		unsigned int m = ion_mask_override ? ion_mask_override
						    : cam->ion_mask;
		/* The buffer has to hold what the CAMIF produces: its
		 * window is 3280x2464 (the sensor's active size plus
		 * the border), and the ISP-IF does not crop it (the
		 * vendor's routing has the crop disabled).  A buffer
		 * for the sensor's active size only is 91,648 bytes
		 * too small, and the frame that comes out is empty. */
		int buf_w = cam->width;
		int buf_h = cam->height;
		int fd_y = ion_alloc_fd((size_t)buf_w * buf_h, m);
		int fd_c = ion_alloc_fd((size_t)buf_w * buf_h / 2, m);
		if (fd_y < 0 || fd_c < 0) {
			printf("[client] dma-buf allocation failed\n");
			return NULL;
		}
		bufs[i] = (unsigned char *)(long)fd_y;
		cbufs.planes[i][1].m.userptr = (unsigned long)fd_c;
	}

	for (i = 0; i < (int)rb.count; i++) {
		struct v4l2_buffer buf;
		memset(&buf, 0, sizeof(buf));
		buf.index = i;
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		buf.memory = V4L2_MEMORY_USERPTR;
		memset(planes, 0, sizeof(planes));
		planes[0].m.userptr = (unsigned long)bufs[i];
		planes[0].length = width * height;
		planes[1].m.userptr = cbufs.planes[i][1].m.userptr;
		planes[1].length = (width * height) / 2;
		buf.m.planes = planes;
		buf.length = 2;
		if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
			printf("[client] QBUF %d: %s\n", i, strerror(errno));
			return NULL;
		}
	}
	printf("[client] %d buffers queued\n", rb.count);
	fflush(stdout);

	cbufs.count = rb.count;
	cbufs.width = width;
	for (i = 0; i < (int)rb.count && i < 8; i++)
		client_buf_fd[i] = (int)bufs[i];
	cbufs.height = height;
	for (i = 0; i < (int)rb.count && i < 8; i++) {
		cbufs.planes[i][0].m.userptr = (unsigned long)bufs[i];
		cbufs.planes[i][0].length = width * height;
		cbufs.planes[i][1].length = (width * height) / 2;
		memset(&cbufs.buf[i], 0, sizeof(cbufs.buf[i]));
		cbufs.buf[i].index = i;
		cbufs.buf[i].type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		cbufs.buf[i].memory = V4L2_MEMORY_USERPTR;
		cbufs.buf[i].m.planes = cbufs.planes[i];
		cbufs.buf[i].length = 2;
	}

	if (ioctl(fd, VIDIOC_STREAMON, &rb.type) < 0) {
		printf("[client] STREAMON: %s\n", strerror(errno));
		return NULL;
	}
	printf("[client] STREAMON ok -- waiting for a frame\n");
	fflush(stdout);

	for (n = 0; ; n++) {
		struct v4l2_buffer buf;
		struct pollfd pfd = { .fd = fd, .events = POLLIN };
		int r;

		/* The vendor's per-frame order: the chromatrix goes to
		 * the CPP first (it carries the frame's buffer
		 * addresses, which change), then the ISP's register
		 * cycle, then the frame is waited for.
		 *
		 * The camera2 HAL brackets its cycle with UPDATE_STREAM
		 * ("I want a frame now") and CONFIG_DONE, and hands the
		 * ISP the buffer for the frame with ENQUEUE_BUF. */
		{
			struct msm_vfe_axi_stream_update_cmd upd;
			int done = 0;

			memset(&upd, 0, sizeof(upd));
			upd.stream_handle = isp_stream_handle;
			upd.update_type = UPDATE_STREAM_REQUEST_FRAMES;
			upd.skip_pattern = NO_SKIP;
			upd.request_frm_num = 1;
			if (ioctl(isp_fd, VIDIOC_MSM_ISP_UPDATE_STREAM,
				  &upd) < 0)
				printf("[isp] UPDATE_STREAM: %s\n",
				       strerror(errno));
			if (ioctl(isp_fd, VIDIOC_MSM_ISP_CONFIG_DONE,
				  &done) < 0)
				printf("[isp] CONFIG_DONE: %s\n",
				       strerror(errno));
		}

		if (cpp_fw_path && strcmp(cpp_fw_path, "-") != 0 &&
		    isp_stream_src == CAMIF_RAW)
			{
			int retry;
			for (retry = 0; retry < 5; retry++) {
				if (cpp_send_chromatrix() == 0) break;
				struct timespec ts = { 0, 50 * 1000 * 1000 };
				nanosleep(&ts, NULL);
			}
		}

		if (replay_cycle)
			isp_replay(cam_cycle, CAM_CYCLE_COUNT,
				   "per-frame cycle");
		r = poll(&pfd, 1, 5000);
		if (r <= 0) {
			printf("[client] no frame after 5s\n");
			continue;
		}
		{
			struct v4l2_plane dqplanes[2];
			memset(dqplanes, 0, sizeof(dqplanes));
			memset(&buf, 0, sizeof(buf));
			buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
			buf.memory = V4L2_MEMORY_USERPTR;
			buf.m.planes = dqplanes;
			buf.length = 2;
			if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
				printf("[client] DQBUF: %s\n", strerror(errno));
				continue;
			}
			printf("[client] plane0: bytesused=%u length=%u data_offset=%u\n",
			       dqplanes[0].bytesused, dqplanes[0].length,
			       dqplanes[0].data_offset);
			printf("[client] plane1: bytesused=%u length=%u data_offset=%u\n",
			       dqplanes[1].bytesused, dqplanes[1].length,
			       dqplanes[1].data_offset);
		}
		{
			/* how often the frames come: with the CAMIF's
			 * pixel clock and the sensor's real rate, the
			 * frame rate shows whether the two agree */
			static struct timespec last;
			struct timespec now;
			double dt;

			clock_gettime(CLOCK_MONOTONIC, &now);
			dt = (now.tv_sec - last.tv_sec) +
			     (now.tv_nsec - last.tv_nsec) / 1e9;
			last = now;
			if (dt > 0)
				printf("[client] the frame rate: %.2f fps"
				       " (%.2f Mpixel/s)\n", 1.0 / dt,
				       3280.0 * 2464.0 / dt / 1e6);
		}
		printf("[client] GOT A FRAME: index=%u bytesused=%u seq=%u\n",
		       buf.index, buf.bytesused, buf.sequence);
		{
			/* Which of our buffers did the ISP actually write
			 * into?  Count the non-zero bytes of each
			 * one: the frame may have landed in a buffer
			 * other than the one that was dequeued.
			 *
			 * The wait matters: the buffer is delivered
			 * when the frame is done, but the pixels
			 * keep landing for a moment afterwards. */
			int bi;
			struct timespec settle = { 0, 300 * 1000 * 1000 };

			nanosleep(&settle, NULL);

			for (bi = 0; bi < (int)cbufs.count && bi < 8; bi++) {
				const unsigned char *p;
				size_t off, nbytes = 0;
				size_t len = (size_t)width * height;

				p = mmap(NULL, len, PROT_READ, MAP_SHARED,
					 (int)bufs[bi], 0);
				if (p == MAP_FAILED) {
					printf("[client] buffer %d: mmap failed\n",
					       bi);
					continue;
				}
				for (off = 0; off < len; off += 4096) {
					size_t k;
					for (k = 0; k < 4096 && off + k < len;
					     k++)
						if (p[off + k])
							nbytes++;
				}
				printf("[client] buffer %d: %u non-zero bytes%s\n",
				       bi, (unsigned)nbytes,
				       bi == (int)buf.index ? " (dequeued)" : "");
				if (nbytes > 0) {
					char name[64];
					FILE *g;

					snprintf(name, sizeof(name),
						 "/data/local/tmp/frame%d.nv12", bi);
					g = fopen(name, "wb");
					if (g) {
						fwrite(p, 1, len, g);
						fclose(g);
					}
				}
				munmap((void *)p, len);
			}
		}
		{
			const char *out = "/data/local/tmp/frame.nv12";
			FILE *f;
			void *p = mmap(NULL, (size_t)cbufs.width * cbufs.height,
				       PROT_READ, MAP_SHARED,
				       (int)bufs[buf.index], 0);
			f = fopen(out, "wb");
			if (f) {
				if (p != MAP_FAILED)
					fwrite(p, 1,
					       (size_t)cbufs.width * cbufs.height,
					       f);
				fclose(f);
				printf("[client] wrote %s (%s)\n", out,
				       p == MAP_FAILED ? "mmap failed" : "ok");
			}
			if (p != MAP_FAILED)
				munmap(p, (size_t)cbufs.width * cbufs.height);
		}
		/* convert to QVGA and store in latest slot */
		{
			const unsigned char *src_y, *src_uv;
			void *p_y, *p_uv;
			size_t y_sz = (size_t)width * height;
			size_t uv_sz = (size_t)width * height / 2;

			{
				struct timespec ts = { 0, 100 * 1000 * 1000 };
				nanosleep(&ts, NULL);
			}
			p_y = mmap(NULL, y_sz, PROT_READ, MAP_SHARED,
				   (int)bufs[buf.index], 0);
			p_uv = mmap(NULL, uv_sz, PROT_READ, MAP_SHARED,
				    (int)cbufs.planes[buf.index][1].m.userptr, 0);
			if (p_y != MAP_FAILED && p_uv != MAP_FAILED) {
				src_y = (const unsigned char *)p_y;
				src_uv = (const unsigned char *)p_uv;
				if (!latest_y) {
					latest_y = malloc(320 * 240);
					latest_uv = malloc(160 * 120 * 2);
				}
				{
					int ox = (width - 1600) / 2;
					int oy = (height - 1200) / 2;
					int dx, dy;
					for (dy = 0; dy < 240; dy++)
						for (dx = 0; dx < 320; dx++) {
							int sx, sy;
							unsigned sum = 0;
							for (sy = 0; sy < 5; sy++)
								for (sx = 0; sx < 5; sx++)
									sum += src_y[(oy + dy*5 + sy) * width + (ox + dx*5 + sx)];
							latest_y[dy * 320 + dx] = (unsigned char)(sum / 25);
						}
				}
				{
					int uv_w = width / 2;
					int uv_h = height / 2;
					int ox = (uv_w - 800) / 2;
					int oy = (uv_h - 600) / 2;
					int dx, dy;
					for (dy = 0; dy < 120; dy++)
						for (dx = 0; dx < 160; dx++) {
							int sx, sy;
							unsigned sum_u = 0, sum_v = 0;
							for (sy = 0; sy < 5; sy++)
								for (sx = 0; sx < 5; sx++) {
									int idx = (oy + dy*5 + sy) * uv_w + (ox + dx*5 + sx);
									sum_u += src_uv[idx * 2];
									sum_v += src_uv[idx * 2 + 1];
								}
							latest_uv[(dy * 160 + dx) * 2] = (unsigned char)(sum_u / 25);
							latest_uv[(dy * 160 + dx) * 2 + 1] = (unsigned char)(sum_v / 25);
						}
				}
				pthread_mutex_lock(&frame_mutex);
				latest_seq++;
				latest_ready = 1;
				pthread_mutex_unlock(&frame_mutex);
			}
			if (p_y != MAP_FAILED) munmap(p_y, y_sz);
			if (p_uv != MAP_FAILED) munmap(p_uv, uv_sz);
		}

		/* re-queue the buffer */
		{
			struct v4l2_plane qplanes[2];
			memset(qplanes, 0, sizeof(qplanes));
			qplanes[0].m.userptr = (unsigned long)bufs[buf.index];
			qplanes[0].length = width * height;
			qplanes[1].m.userptr = cbufs.planes[buf.index][1].m.userptr;
			qplanes[1].length = (width * height) / 2;
			cbufs.buf[buf.index].m.planes = qplanes;
			cbufs.buf[buf.index].length = 2;
			ioctl(fd, VIDIOC_QBUF, &cbufs.buf[buf.index]);
		}

		fflush(stdout);
	}

	ioctl(fd, VIDIOC_STREAMOFF, &rb.type);
	printf("[client] done\n");
	return NULL;
}

static void *socket_thread(void *arg)
{
	int sfd, cfd;
	/* socket thread */
	struct sockaddr_un addr;
	char buf[1];

	unlink("/data/krux/camd.sock");
	mkdir("/data/krux", 0755);
	sfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sfd < 0) { perror("socket"); return NULL; }
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, "/data/krux/camd.sock", sizeof(addr.sun_path) - 1);
	if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); return NULL; }
	listen(sfd, 4);
	chmod("/data/krux/camd.sock", 0666);

	for (;;) {
		cfd = accept(sfd, NULL, NULL);
		if (cfd < 0) continue;
		for (;;) {
			int r = read(cfd, buf, 1);
			if (r <= 0) break;
			if (buf[0] == 'F') {
				unsigned char hdr[32];
				memset(hdr, 0, 32);
				memcpy(hdr, "KCF1", 4);
				pthread_mutex_lock(&frame_mutex);
				int seq = latest_seq;
				int ready = latest_ready;
				if (ready && latest_y && latest_uv) {
					hdr[31] = 0;
					pthread_mutex_unlock(&frame_mutex);
					hdr[4] = seq & 0xff; hdr[5] = (seq>>8)&0xff;
					hdr[6] = (seq>>16)&0xff; hdr[7] = (seq>>24)&0xff;
					hdr[8] = 320 & 0xff; hdr[9] = (320>>8)&0xff;
					hdr[10] = 240 & 0xff; hdr[11] = (240>>8)&0xff;
					hdr[12] = 320 & 0xff; hdr[13] = (320>>8)&0xff;
					hdr[14] = 240 & 0xff; hdr[15] = (240>>8)&0xff;
					hdr[16] = 320 & 0xff; hdr[17] = (320>>8)&0xff;
					hdr[20] = 160 & 0xff; hdr[21] = (160>>8)&0xff;
					hdr[24] = (320*240) & 0xff; hdr[25] = ((320*240)>>8)&0xff;
					hdr[26] = ((320*240)>>16)&0xff; hdr[27] = ((320*240)>>24)&0xff;
					hdr[28] = (160*120*2) & 0xff; hdr[29] = ((160*120*2)>>8)&0xff;
					hdr[30] = ((160*120*2)>>16)&0xff; hdr[31] = ((160*120*2)>>24)&0xff;
					write(cfd, hdr, 32);
					pthread_mutex_lock(&frame_mutex);
					write(cfd, latest_y, 320*240);
					write(cfd, latest_uv, 160*120*2);
					pthread_mutex_unlock(&frame_mutex);
				} else {
					hdr[31] = 1;
					pthread_mutex_unlock(&frame_mutex);
					write(cfd, hdr, 32);
				}
			}
		}
		close(cfd);
	}
	return NULL;
}

int main(int argc, char **argv)
{
	pthread_t server, client;
	int wh[2] = { 640, 480 };
	signal(SIGPIPE, SIG_IGN);

	setvbuf(stdout, NULL, _IONBF, 0);
	if (argc > 2) {
		wh[0] = atoi(argv[1]);
		wh[1] = atoi(argv[2]);
	}
	if (argc > 3)
		ion_mask_override = strtoul(argv[3], NULL, 0);
	if (argc > 4 && !strcmp(argv[4], "front"))
		cam = &cam_front;
	if (argc > 5)
		front_cmd_count = atoi(argv[5]);
	if (argc > 6)
		cpp_fw_path = argv[6];
	if (argc > 7)
		isp_stream_src = strtoul(argv[7], NULL, 0);
	if (argc > 8 && !strcmp(argv[8], "binned")) {
		cam = &cam_binned;
	}
	if (argc > 9 && !strcmp(argv[9], "nocfg"))
		replay_config = 0;
	if (argc > 10 && !strcmp(argv[10], "nocycle"))
		replay_cycle = 0;
	if (argc > 11)
		cam_vc = atoi(argv[11]);
	if (argc > 12)
		cam_dt = strtoul(argv[12], NULL, 0);
	if (argc > 13)
		cam_cid = atoi(argv[13]);
	if (argc > 14)
		cam_csid = atoi(argv[14]);
	if (argc > 15)
		frames_to_wait = atoi(argv[15]);
	if (argc > 16)
		isp_vfe = atoi(argv[16]);
	if (argc > 18)
		pix_clk_override = strtoul(argv[18], NULL, 0);
	if (argc > 19)
		cam_dt_other = strtoul(argv[19], NULL, 0);
	if (argc > 20)
		lut_cid0 = atoi(argv[20]);
	if (argc > 21)
		lut_cid1 = atoi(argv[21]);
	if (argc > 22)
		poke_reg = strtoul(argv[22], NULL, 0);
	if (argc > 23)
		poke_val = strtoul(argv[23], NULL, 0);
	if (argc > 24)
		two_cids = atoi(argv[24]);
	if (argc > 25)
		use_live_sensor_seq = atoi(argv[25]);
	if (argc > 26)
		crop_enable = atoi(argv[26]);
	if (argc > 27)
		send_capture_config = atoi(argv[27]);
	if (pix_clk_override)
		printf("[isp] pixel clock override %u\n", pix_clk_override);
	if (argc > 17)
		ispif_vfe = atoi(argv[17]);
	printf("[isp] ISP subdev %d, ISP-IF VFE %d\n",
		isp_vfe ? 14 : 13, ispif_vfe);
	printf("[ispif] virtual channel (cid) %d, csid %d\n", cam_cid, cam_csid);
	printf("[mipi] channel %d: data type 0x%02x (and 0x%02x on the other channel)\n", cam_cid, cam_dt, cam_dt_other);
	printf("[mipi] the CSID's LUT: cid %d = 0x%02x, cid %d = 0x%02x\n",
	       lut_cid0, cam_dt, lut_cid1, cam_dt_other);
	printf("[sensor] the sensor's registers: %s\n",
	       use_live_sensor_seq == 1 ? "the vendor's live list" :
	       use_live_sensor_seq == 2 ? "the page sequence + the live list" :
	       "the captured arrays");
	printf("[isp] the capture's configuration: %s\n",
	       send_capture_config ? "sent" : "not sent");
	printf("[ispif] the crop: %s\n",
	       crop_enable ? "enabled" : "disabled (the vendor's own setting)");
	printf("[isp] replay: initial configuration %s, per-frame cycle %s\n",
	       replay_config ? "on" : "off", replay_cycle ? "on" : "off");
	printf("[isp] stream source: %u (%s)\n", isp_stream_src,
	       isp_stream_src == PIX_ENCODER ? "PIX_ENCODER" :
	       isp_stream_src == PIX_VIEWFINDER ? "PIX_VIEWFINDER" :
	       isp_stream_src == CAMIF_RAW ? "CAMIF_RAW" :
	       isp_stream_src == IDEAL_RAW ? "IDEAL_RAW" : "other");
	printf("=== camera: %s (%s, subdev %s) ===\n", cam->name,
	       cam->sensor_node, cam->sensor_subdev);

	sensor_fd = open(cam->sensor_subdev, O_RDWR);
	if (sensor_fd < 0) {
		printf("[sensor] open %s: %s\n", cam->sensor_subdev, strerror(errno));
		return 1;
	}
	printf("[sensor] %s open\n", cam->sensor_subdev);

	pthread_create(&server, NULL, server_thread, NULL);
	pthread_create(&client, NULL, client_thread, wh);
	{
		pthread_t sockt;
		pthread_create(&sockt, NULL, socket_thread, NULL);
	}

	pthread_join(client, NULL);
	pthread_cancel(server);
	return 0;
}
