/*
 * imaging7.c -- our own imaging server and capture client for the Nexus 5.
 *
 * The CAF camera_v2 stack has no userspace of its own: every V4L2 request
 * on a sensor node is posted as an event to a userspace "imaging server"
 * and the kernel blocks until that server acknowledges it
 * (drivers/media/platform/msm/camera_v2/msm.c, msm_post_event()).
 *
 * So the device is driven by two threads in one process:
 *
 *   the server thread
 *     - owns /dev/video0, the "msm-config" node
 *     - subscribes to the event type MSM_CAMERA_V4L2_EVENT_TYPE for the
 *       event ids 1..7 (this tree's v4l2_event_subscribed() matches type
 *       AND id exactly -- there is no "id == 0 means all" wildcard)
 *     - does the work for each command and acknowledges it with
 *       MSM_CAM_V4L2_IOCTL_CMD_ACK (_IOW('V', 224, struct v4l2_event))
 *
 *   the client thread
 *     - owns /dev/video1, the rear sensor's node
 *     - S_FMT, REQBUFS, QBUF, STREAMON, DQBUF, exactly as any V4L2 client
 *
 * The kernel's sensor driver for the IMX179 is a stub: it has the power
 * sequence but no mode tables, so the server programs the sensor itself,
 * through the kernel's own sensor-config ioctl on the sensor subdev
 * (VIDIOC_MSM_SENSOR_CFG):
 *
 *     CFG_POWER_UP          power the sensor (clock, regulators, GPIOs)
 *     CFG_WRITE_I2C_ARRAY   write the mode's register settings
 *
 * The register settings are in standalone/cam/imx179_regs.h.
 *
 * Two ABI traps, both found by probing the device rather than by guessing:
 *
 *  - the ioctl number encodes the size of struct v4l2_event, and the
 *    kernel's struct is 128 bytes where the host's header gives 136 (the
 *    host's struct timespec is 64-bit, the kernel's is not, and the
 *    union inside is laid out differently).  Probing every size shows
 *    that only 128 is accepted; everything else is EINVAL.
 *  - the field offsets have to be read off a live event; they are
 *    documented at the bottom of this file.
 *
 * Build:
 *   arm-linux-gnueabihf-gcc -O2 -static -o imaging7 imaging7.c
 * Run (needs root):
 *   adb shell /data/local/tmp/imaging7 [width] [height] [outfile]
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
#include <time.h>
#include <unistd.h>

#include "v4l2_caf.h"
#include "imx179_regs.h"
#include "isp.h"
#include "ion.h"
#include "mipi.h"
#include "sensor.h"

/* ------------------------------------------------------------------ *
 * this tree's constants (include/media/msm_cam_sensor.h, msmb_camera.h)
 * ------------------------------------------------------------------ */
#define MSM_CAMERA_V4L2_EVENT_TYPE (0x08000000 + 0x00002000)
#define MSM_CAMERA_EVENT_MIN       0
#define MSM_CAMERA_NEW_SESSION     (MSM_CAMERA_EVENT_MIN + 1)
#define MSM_CAMERA_DEL_SESSION     (MSM_CAMERA_EVENT_MIN + 2)
#define MSM_CAMERA_SET_PARM        (MSM_CAMERA_EVENT_MIN + 3)
#define MSM_CAMERA_GET_PARM        (MSM_CAMERA_EVENT_MIN + 4)
#define MSM_CAMERA_MAPPING_CFG     (MSM_CAMERA_EVENT_MIN + 5)
#define MSM_CAMERA_MAPPING_SES     (MSM_CAMERA_EVENT_MIN + 6)
#define MSM_CAMERA_MSM_NOTIFY      (MSM_CAMERA_EVENT_MIN + 7)

#define MSM_CAMERA_CMD_SUCESS      0x00000001

/* the commands the camera node posts (data.command) */
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
#define PRIV_STREAM_INFO_SYNC (0x08000000 + 13)

/* the event, as the kernel lays it out (128 bytes) */
#define EVSZ 128
#define DQ_IOCTL  _IOR('V', 89, char[EVSZ])
#define ACK_IOCTL _IOW('V', 224, char[EVSZ])

/* offsets inside the event */
#define OFF_CMD     8
#define OFF_STATUS  12
#define OFF_SESSION 16
#define OFF_STREAM  20
#define OFF_ARG     36
#define OFF_RET     40
#define OFF_ID      88

/* ------------------------------------------------------------------ *
 * the sensor config ioctl
 * ------------------------------------------------------------------ */
#define BASE_VIDIOC_PRIVATE 192

enum msm_sensor_cfg_type {
	CFG_SET_SLAVE_INFO = 0,
	CFG_SLAVE_READ_I2C,
	CFG_WRITE_I2C_ARRAY,
	CFG_SLAVE_WRITE_I2C_ARRAY,
	CFG_WRITE_I2C_SEQ_ARRAY,
	CFG_POWER_UP,
	CFG_POWER_DOWN,
	CFG_SET_STOP_STREAM_SETTING,
	CFG_GET_SENSOR_INFO,
	CFG_GET_SENSOR_INIT_PARAMS,
	CFG_SET_INIT_SETTING,
	CFG_SET_RESOLUTION,
	CFG_SET_STOP_STREAM,
	CFG_SET_START_STREAM,
	CFG_OIS_ON,
	CFG_OIS_OFF,
	CFG_GET_OIS_INFO,
	CFG_SET_OIS_MODE,
	CFG_OIS_MOVE_LENS
};

enum msm_camera_i2c_reg_addr_type {
	MSM_CAMERA_I2C_BYTE_ADDR = 1,
	MSM_CAMERA_I2C_WORD_ADDR,
	MSM_CAMERA_I2C_3B_ADDR
};

enum msm_camera_i2c_data_type {
	MSM_CAMERA_I2C_BYTE_DATA = 1,
	MSM_CAMERA_I2C_WORD_DATA,
	MSM_CAMERA_I2C_SET_BYTE_MASK,
	MSM_CAMERA_I2C_UNSET_BYTE_MASK,
	MSM_CAMERA_I2C_SET_WORD_MASK,
	MSM_CAMERA_I2C_UNSET_WORD_MASK,
	MSM_CAMERA_I2C_SET_BYTE_WRITE_MASK_DATA
};

/* struct sensorb_cfg_data: cfgtype, union (76 bytes), setting_size */
struct sensorb_cfg_data {
	int cfgtype;
	unsigned char cfg[76];
	unsigned int setting_size;
};

#define SENSOR_CFG_IOCTL _IOWR('V', BASE_VIDIOC_PRIVATE + 1, struct sensorb_cfg_data)

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

#define SENSOR_SUBDEV "/dev/v4l-subdev11"	/* the imx179 */
#define CONFIG_NODE  "/dev/video0"		/* msm-config */
#define SENSOR_NODE  "/dev/video1"		/* msm-sensor, the rear camera */

/* ------------------------------------------------------------------ */
static unsigned rd32(const unsigned char *p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24);
}
static void wr32(unsigned char *p, unsigned v)
{
	p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

static const char *cmd_name(unsigned c)
{
	switch (c) {
	case PRIV_S_CROP: return "S_CROP";
	case PRIV_G_CROP: return "G_CROP";
	case PRIV_G_FMT:  return "G_FMT";
	case PRIV_S_FMT:  return "S_FMT";
	case PRIV_TRY_FMT: return "TRY_FMT";
	case PRIV_METADATA: return "METADATA";
	case PRIV_QUERY_CAP: return "QUERY_CAP";
	case PRIV_STREAM_ON: return "STREAM_ON";
	case PRIV_STREAM_OFF: return "STREAM_OFF";
	case PRIV_NEW_STREAM: return "NEW_STREAM";
	case PRIV_DEL_STREAM: return "DEL_STREAM";
	case PRIV_SHUTDOWN: return "SHUTDOWN";
	case PRIV_STREAM_INFO_SYNC: return "STREAM_INFO_SYNC";
	default: return "?";
	}
}

/* ------------------------------------------------------------------ *
 * the sensor: power up and program the mode
 * ------------------------------------------------------------------ */
static int sensor_fd = -1;

static int sensor_cfg(int cfgtype, void *setting, unsigned int size)
{
	struct sensorb_cfg_data c;
	memset(&c, 0, sizeof(c));
	c.cfgtype = cfgtype;
	c.setting_size = size;
	if (setting)
		memcpy(c.cfg, setting, size);
	if (ioctl(sensor_fd, SENSOR_CFG_IOCTL, &c) < 0) {
		printf("[sensor] cfg %d failed: %s\n", cfgtype, strerror(errno));
		return -1;
	}
	return 0;
}

static int sensor_write_regs(const struct imx179_reg *regs, int n)
{
	struct msm_camera_i2c_reg_setting set;
	struct msm_camera_i2c_reg_array *arr;
	int i;

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
	set.data_type = MSM_CAMERA_I2C_BYTE_DATA;
	set.delay = 0;

	/* the kernel copies reg_setting->reg_setting and the array out of
	 * user space, so both have to be readable by it */
	if (sensor_cfg(CFG_WRITE_I2C_ARRAY, &set, sizeof(set)) < 0) {
		free(arr);
		return -1;
	}
	free(arr);
	printf("[sensor] wrote %d registers\n", n);
	return 0;
}

static int sensor_power_up(void)
{
	if (sensor_cfg(CFG_POWER_UP, NULL, 0) < 0)
		return -1;
	printf("[sensor] powered up\n");
	return 0;
}

/* the IMX179 starts streaming when 0x0100 is set to 1 */
static const struct imx179_reg stream_on_regs[] = { { 0x0100, 0x01, 0 } };
static const struct imx179_reg stream_off_regs[] = { { 0x0100, 0x00, 0 } };

static int sensor_program_mode(void)
{
	if (sensor_power_up() < 0)
		return -1;
	return sensor_write_regs(IMX179_MODE_TABLE, IMX179_MODE_TABLE_LEN);
}

/* set once the server has subscribed: the client must not open the
 * sensor node before that, or the kernel answers EIO ("daemon hasn't
 * subscribed yet") */
static volatile int server_ready;

/* The client's buffers, so that the server can hand them to the ISP.
 * The ISP needs the filled v4l2_buffer (with the user pointers), not
 * just the addresses: it maps them when the buffer is prepared. */
struct client_buffers {
	volatile int	count;
	int		width;
	int		height;
	struct v4l2_buffer	buf[8];
	struct v4l2_plane	planes[8][3];
};
static struct client_buffers cbufs;


/* ------------------------------------------------------------------ *
 * the buffers: dma-bufs from the ION allocator
 * ------------------------------------------------------------------ */
static unsigned int ion_heap_mask = ION_MASK_SYSTEM;

/* Allocate `len` bytes from ION and return a dma-buf fd (or -1). */
static int ion_alloc_fd(size_t len)
{
	struct ion_allocation_data ad;
	struct ion_fd_data fd;
	int ion, r = -1;

	ion = open("/dev/ion", O_RDWR);
	if (ion < 0) {
		printf("[ion] open /dev/ion: %s\n", strerror(errno));
		return -1;
	}
	memset(&ad, 0, sizeof(ad));
	ad.len = len;
	ad.align = 4096;
	ad.heap_id_mask = ion_heap_mask;
	ad.flags = 0;
	if (ioctl(ion, ION_IOC_ALLOC, &ad) < 0) {
		printf("[ion] ION_IOC_ALLOC (mask 0x%x): %s\n",
		       ion_heap_mask, strerror(errno));
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


/* ------------------------------------------------------------------ *
 * the MIPI link: CSIPHY and CSID
 * ------------------------------------------------------------------ */
static int csiphy_fd = -1;
static int csid_fd = -1;

/* Configure the PHY and the receiver for the rear camera's four lanes.
 * The lane layout comes from the device tree; the VC lookup table says
 * which virtual channel carries the sensor's data. */
static int mipi_config(void)
{
	struct csiphy_cfg_data cphy;
	struct msm_camera_csiphy_params cphy_params;
	struct csid_cfg_data cid;
	struct msm_sensor_csid_cfg_params cid_params;
	struct msm_camera_csid_params cparams;
	struct msm_camera_csid_lut_params lut;
	struct msm_camera_csid_vc_cfg vc;

	if (csiphy_fd < 0)
		csiphy_fd = open(CSIPHY_NODE, O_RDWR);
	if (csiphy_fd < 0) {
		printf("[mipi] open %s: %s\n", CSIPHY_NODE, strerror(errno));
		return -1;
	}
	if (csid_fd < 0)
		csid_fd = open(CSID_NODE, O_RDWR);
	if (csid_fd < 0) {
		printf("[mipi] open %s: %s\n", CSID_NODE, strerror(errno));
		return -1;
	}

	/* CSIPHY_INIT maps the PHY's registers; without it the base is
	 * NULL and the lane configuration fails with EINVAL. */
	memset(&cphy, 0, sizeof(cphy));
	cphy.cfgtype = CSIPHY_INIT;
	if (ioctl(csiphy_fd, VIDIOC_MSM_CSIPHY_IO_CFG, &cphy) < 0)
		printf("[mipi] CSIPHY_INIT: %s\n", strerror(errno));
	else
		printf("[mipi] CSIPHY initialised\n");

	/* the PHY: four lanes, mask 0x1f */
	memset(&cphy_params, 0, sizeof(cphy_params));
	cphy_params.lane_cnt = CAM_LANE_CNT;
	cphy_params.settle_cnt = 14;
	cphy_params.lane_mask = CAM_LANE_MASK;
	cphy_params.combo_mode = 0;
	cphy_params.csid_core = 0;
	memset(&cphy, 0, sizeof(cphy));
	cphy.cfgtype = CSIPHY_CFG;
	cphy.cfg.csiphy_params = &cphy_params;
	cphy.cfg_params_size = sizeof(cphy_params);
	if (ioctl(csiphy_fd, VIDIOC_MSM_CSIPHY_IO_CFG, &cphy) < 0)
		printf("[mipi] CSIPHY_CFG: %s\n", strerror(errno));
	else
		printf("[mipi] CSIPHY configured: %d lanes, mask 0x%x\n",
		       cphy_params.lane_cnt, cphy_params.lane_mask);

	/* CSID_INIT likewise maps the receiver's registers */
	memset(&cid, 0, sizeof(cid));
	cid.cfgtype = CSID_INIT;
	if (ioctl(csid_fd, VIDIOC_MSM_CSID_IO_CFG, &cid) < 0)
		printf("[mipi] CSID_INIT: %s\n", strerror(errno));
	else
		printf("[mipi] CSID initialised\n");

	/* the receiver: the same lanes, and the VC table */
	memset(&vc, 0, sizeof(vc));
	vc.cid = 0;		/* virtual channel 0 */
	vc.dt = 0x15;		/* MIPI CSI-2 data type RAW10 (the IMX179
				 * sends 10-bit Bayer) */
	vc.decode_format = 1;
	memset(&lut, 0, sizeof(lut));
	lut.num_cid = 1;
	lut.vc_cfg[0] = &vc;
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

/* The ISP subdevice: both v4l-subdev13 and v4l-subdev14 are called
 * "vfe"; try them in turn. */
static int isp_open(void)
{
	static const char *cands[] = {
		"/dev/v4l-subdev13", "/dev/v4l-subdev14", NULL
	};
	int i;

	if (isp_fd >= 0)
		return isp_fd;
	for (i = 0; cands[i]; i++) {
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

/* Give the ISP the buffers, create the stream and start it. */
static int isp_start(void)
{
	struct msm_isp_buf_request breq;
	struct msm_vfe_axi_stream_request_cmd scmd;
	struct msm_vfe_axi_stream_cfg_cmd ccmd;
	int i;

	if (isp_open() < 0)
		return -1;
	if (!cbufs.count) {
		printf("[isp] no client buffers yet\n");
		return -1;
	}

	/* 1. the buffer manager allocates the ISP's buffers */
	memset(&breq, 0, sizeof(breq));
	breq.session_id = 1;
	breq.stream_id = 0;
	breq.num_buf = cbufs.count;
	breq.handle = 0;
	breq.buf_type = ISP_SHARE_BUF;
	if (ioctl(isp_fd, VIDIOC_MSM_ISP_REQUEST_BUF, &breq) < 0) {
		printf("[isp] REQUEST_BUF: %s\n", strerror(errno));
		return -1;
	}
	printf("[isp] REQUEST_BUF ok, handle=%u\n", breq.handle);

	/* 1b. where the ISP reads from: the CAMIF, i.e. the CSID, with
	 * the sensor's frame geometry.  The IMX179's full-resolution mode
	 * is 3264x2448. */
	{
		struct msm_vfe_input_cfg icfg;
		memset(&icfg, 0, sizeof(icfg));
		icfg.input_src = VFE_PIX_0;
		icfg.input_pix_clk = 320000000;
		icfg.d.pix_cfg.input_mux = CAMIF;
		icfg.d.pix_cfg.camif_cfg.camif_input = CAMIF_MIPI_INPUT;
		icfg.d.pix_cfg.camif_cfg.lines_per_frame = 2448;
		icfg.d.pix_cfg.camif_cfg.pixels_per_line = 3264;
		icfg.d.pix_cfg.camif_cfg.first_pixel = 0;
		icfg.d.pix_cfg.camif_cfg.last_pixel = 3263;
		icfg.d.pix_cfg.camif_cfg.first_line = 0;
		icfg.d.pix_cfg.camif_cfg.last_line = 2447;
		icfg.d.pix_cfg.camif_cfg.epoch_line0 = 0;
		icfg.d.pix_cfg.camif_cfg.epoch_line1 = 2448;
		if (ioctl(isp_fd, VIDIOC_MSM_ISP_INPUT_CFG, &icfg) < 0)
			printf("[isp] INPUT_CFG: %s\n", strerror(errno));
		else
			printf("[isp] input configured: CAMIF, 3264x2448\n");
	}

	/* 2. the stream itself */
	memset(&scmd, 0, sizeof(scmd));
	scmd.session_id = 1;
	scmd.stream_id = 0;
	scmd.output_format = ISP_PIX_FMT_NV12;
	scmd.stream_src = CAMIF_RAW;
	scmd.init_frame_drop = 0;
	scmd.frame_skip_pattern = NO_SKIP;
	/* NV12: plane 0 is Y, plane 1 is the interleaved CbCr */
	scmd.plane_cfg[0].output_width = cbufs.width;
	scmd.plane_cfg[0].output_height = cbufs.height;
	scmd.plane_cfg[0].output_stride = cbufs.width;
	scmd.plane_cfg[0].output_scan_lines = cbufs.height;
	scmd.plane_cfg[0].output_plane_format = Y_PLANE;
	scmd.plane_cfg[0].plane_addr_offset = 0;
	scmd.plane_cfg[0].csid_src = 0;
	scmd.plane_cfg[0].rdi_cid = 0;
	scmd.plane_cfg[1].output_width = cbufs.width;
	scmd.plane_cfg[1].output_height = cbufs.height;
	scmd.plane_cfg[1].output_stride = cbufs.width;
	scmd.plane_cfg[1].output_scan_lines = cbufs.height;
	scmd.plane_cfg[1].output_plane_format = CBCR_PLANE;
	scmd.plane_cfg[1].plane_addr_offset = 0;
	scmd.plane_cfg[1].csid_src = 0;
	scmd.plane_cfg[1].rdi_cid = 0;
	if (ioctl(isp_fd, VIDIOC_MSM_ISP_REQUEST_STREAM, &scmd) < 0) {
		printf("[isp] REQUEST_STREAM: %s\n", strerror(errno));
		return -1;
	}
	printf("[isp] REQUEST_STREAM ok, handle=%u\n", scmd.axi_stream_handle);

	/* 3. hand the client's vb2 buffers to the ISP */
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

	/* 4. start it */
	memset(&ccmd, 0, sizeof(ccmd));
	ccmd.num_streams = 1;
	ccmd.stream_handle[0] = scmd.axi_stream_handle;
	ccmd.cmd = START_STREAM;
	if (ioctl(isp_fd, VIDIOC_MSM_ISP_CFG_STREAM, &ccmd) < 0) {
		printf("[isp] CFG_STREAM: %s\n", strerror(errno));
		return -1;
	}
	printf("[isp] stream started\n");
	return 0;
}

/* ------------------------------------------------------------------ *
 * the server thread
 * ------------------------------------------------------------------ */
static void *server_thread(void *arg)
{
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
		unsigned char buf[EVSZ];
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
		printf("[server] event id=%u cmd=0x%08x(%s) session=%u stream=%u\n",
		       rd32(buf + OFF_ID), cmd, cmd_name(cmd),
		       rd32(buf + OFF_SESSION), rd32(buf + OFF_STREAM));
		fflush(stdout);

		switch (cmd) {
		case PRIV_S_FMT:
			/* the client wants this format: program the sensor */
			mipi_config();
			sensor_program_mode();
			break;
		case PRIV_STREAM_ON:
			sensor_write_regs(stream_on_regs, 1);
			isp_start();
			break;
		case PRIV_STREAM_OFF:
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
static void *client_thread(void *arg)
{
	int *wh = (int *)arg;
	int width = wh[0];
	int height = wh[1];
	int fd;
	struct v4l2_format fmt;
	struct v4l2_requestbuffers rb;
	struct v4l2_plane planes[3];
	unsigned char *bufs[4];
	int i, n;

	while (!server_ready) {
		struct timespec ts = { 0, 20 * 1000 * 1000 };
		nanosleep(&ts, NULL);
	}
	fd = open(SENSOR_NODE, O_RDWR);
	if (fd < 0) {
		printf("[client] open %s: %s\n", SENSOR_NODE, strerror(errno));
		return NULL;
	}
	printf("[client] open %s SUCCEEDED -- the handshake completed\n", SENSOR_NODE);
	fflush(stdout);

	/* S_PARM is what creates the kernel's stream object for this
	 * queue; without it the buffers have no stream to attach to
	 * ("msm_vb2_buf_init: Couldn't find stream"). */
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
		/* two planes: Y and the interleaved CbCr, each a dma-buf */
		int fd_y = ion_alloc_fd((size_t)width * height);
		int fd_c = ion_alloc_fd((size_t)width * height / 2);
		if (fd_y < 0 || fd_c < 0) {
			printf("[client] dma-buf allocation failed\n");
			return NULL;
		}
		bufs[i] = (unsigned char *)(long)fd_y;	/* the Y fd */
		cbufs.planes[i][1].m.userptr = (unsigned long)fd_c;
	}

	for (i = 0; i < (int)rb.count; i++) {
		struct v4l2_buffer buf;
		memset(&buf, 0, sizeof(buf));
		buf.index = i;
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		buf.memory = V4L2_MEMORY_USERPTR;
		memset(planes, 0, sizeof(planes));
		planes[0].m.userptr = (unsigned long)bufs[i];	/* the Y fd */
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

	/* the server needs these to give them to the ISP */
	cbufs.count = rb.count;
	cbufs.width = width;
	cbufs.height = height;
	for (i = 0; i < (int)rb.count && i < 8; i++) {
		cbufs.planes[i][0].m.userptr = (unsigned long)bufs[i];
		cbufs.planes[i][0].length = width * height;
		cbufs.planes[i][1].m.userptr = cbufs.planes[i][1].m.userptr;
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

	for (n = 0; n < 10; n++) {
		struct v4l2_buffer buf;
		struct pollfd pfd = { .fd = fd, .events = POLLIN };
		int r = poll(&pfd, 1, 5000);
		if (r <= 0) {
			printf("[client] no frame after 5s\n");
			continue;
		}
		memset(&buf, 0, sizeof(buf));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		buf.memory = V4L2_MEMORY_USERPTR;
		if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
			printf("[client] DQBUF: %s\n", strerror(errno));
			continue;
		}
		printf("[client] got a frame: index=%u bytesused=%u seq=%u\n",
		       buf.index, buf.bytesused, buf.sequence);
		{
			/* the buffer is a dma-buf: map it to read the pixels */
			const char *out = "/data/local/tmp/frame.nv12";
			FILE *f;
			void *p = mmap(NULL, (size_t)width * height,
				       PROT_READ, MAP_SHARED,
				       (int)bufs[buf.index], 0);
			f = fopen(out, "wb");
			if (f) {
				if (p != MAP_FAILED)
					fwrite(p, 1, (size_t)width * height, f);
				fclose(f);
				printf("[client] wrote %s (%s)\n", out,
				       p == MAP_FAILED ? "mmap failed" : "ok");
			}
			if (p != MAP_FAILED)
				munmap(p, (size_t)width * height);
		}
		fflush(stdout);
		/* requeue and stop */
		{
			struct v4l2_buffer q;
			memset(&q, 0, sizeof(q));
			q.index = buf.index;
			q.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
			q.memory = V4L2_MEMORY_USERPTR;
			memset(planes, 0, sizeof(planes));
			planes[0].m.userptr = (unsigned long)bufs[buf.index];
			planes[0].length = width * height;
			planes[1].m.userptr = cbufs.planes[buf.index][1].m.userptr;
			planes[1].length = (width * height) / 2;
			q.m.planes = planes;
			q.length = 2;
			ioctl(fd, VIDIOC_QBUF, &q);
		}
		break;
	}
	ioctl(fd, VIDIOC_STREAMOFF, &rb.type);
	printf("[client] done\n");
	return NULL;
}

int main(int argc, char **argv)
{
	pthread_t server, client;
	int wh[2] = { 640, 480 };

	setvbuf(stdout, NULL, _IONBF, 0);
	if (argc > 2) {
		wh[0] = atoi(argv[1]);
		wh[1] = atoi(argv[2]);
	}
	if (argc > 3)
		ion_heap_mask = strtoul(argv[3], NULL, 0);

	sensor_fd = open(SENSOR_SUBDEV, O_RDWR);
	if (sensor_fd < 0) {
		printf("[sensor] open %s: %s\n", SENSOR_SUBDEV, strerror(errno));
		return 1;
	}
	printf("[sensor] %s open\n", SENSOR_SUBDEV);

	pthread_create(&server, NULL, server_thread, NULL);
	pthread_create(&client, NULL, client_thread, wh);

	pthread_join(client, NULL);
	pthread_cancel(server);
	return 0;
}
