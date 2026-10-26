/*
 * isp.h -- the ISP (VFE) side of our imaging server.
 *
 * The camera_v2 stack does not start the ISP by itself: the imaging
 * server has to ask for the buffers, ask for the stream and then start
 * it, through the ISP subdevice's own ioctls
 * (drivers/media/platform/msm/camera_v2/isp/msm_isp_util.c):
 *
 *   VIDIOC_MSM_ISP_REQUEST_BUF     the buffer manager allocates buffers
 *   VIDIOC_MSM_ISP_REQUEST_STREAM  the ISP creates the stream
 *   VIDIOC_MSM_ISP_ENQUEUE_BUF     the client's vb2 buffers are given to
 *                                  the ISP
 *   VIDIOC_MSM_ISP_CFG_STREAM      START_STREAM
 *
 * This is the "no chromatrix" path: the ISP's algorithm settings (the
 * vendor's chromatix blob) are not sent, so the ISP runs with whatever
 * defaults it has.  Whether it streams at all is the experiment.
 */
#ifndef ISP_H
#define ISP_H

#include <stdint.h>

#include "v4l2_caf.h"

#define BASE_VIDIOC_PRIVATE 192

enum msm_isp_buf_type {
	ISP_PRIVATE_BUF = 0,
	ISP_SHARE_BUF,
	MAX_ISP_BUF_TYPE
};

enum msm_vfe_plane_fmt {
	Y_PLANE = 0,
	CB_PLANE,
	CR_PLANE,
	CRCB_PLANE,
	CBCR_PLANE,
	VFE_PLANE_FMT_MAX
};

enum msm_vfe_axi_stream_src {
	PIX_ENCODER = 0,
	PIX_VIEWFINDER,
	CAMIF_RAW,
	IDEAL_RAW,
	RDI_INTF_0,
	RDI_INTF_1,
	RDI_INTF_2,
	VFE_AXI_SRC_MAX
};

enum msm_vfe_axi_stream_cmd {
	STOP_STREAM = 0,
	START_STREAM,
	STOP_IMMEDIATELY
};

enum msm_vfe_frame_skip_pattern {
	NO_SKIP = 0,
	EVERY_2FRAME,
	EVERY_3FRAME,
	EVERY_4FRAME,
	EVERY_5FRAME,
	EVERY_6FRAME,
	EVERY_7FRAME,
	EVERY_8FRAME,
	EVERY_9FRAME,
	EVERY_10FRAME,
	EVERY_11FRAME,
	EVERY_12FRAME,
	EVERY_13FRAME,
	EVERY_14FRAME,
	EVERY_15FRAME,
	EVERY_16FRAME,
	EVERY_17FRAME,
	EVERY_18FRAME,
	EVERY_19FRAME,
	EVERY_20FRAME,
	EVERY_21FRAME,
	EVERY_22FRAME,
	EVERY_23FRAME,
	EVERY_24FRAME,
	EVERY_25FRAME,
	EVERY_26FRAME,
	EVERY_27FRAME,
	EVERY_28FRAME,
	EVERY_29FRAME,
	EVERY_30FRAME,
	EVERY_31FRAME,
	MAX_SKIP_PATTERN
};

#define MAX_PLANES_PER_STREAM 3
#define MAX_NUM_STREAM 7

struct msm_vfe_axi_plane_cfg {
	uint32_t output_width;
	uint32_t output_height;
	uint32_t output_stride;
	uint32_t output_scan_lines;
	uint32_t output_plane_format;
	uint32_t plane_addr_offset;
	uint8_t csid_src;
	uint8_t rdi_cid;
};

/* the ISP's input: which CSID it reads, and the sensor's geometry */
enum msm_vfe_input_src {
	VFE_PIX_0 = 0,
	VFE_RAW_0,
	VFE_RAW_1,
	VFE_RAW_2,
	VFE_SRC_MAX
};

enum msm_vfe_camif_input {
	CAMIF_DISABLED = 0,
	CAMIF_PAD_REG_INPUT,
	CAMIF_MIDDI_INPUT,
	CAMIF_MIPI_INPUT
};

enum msm_vfe_inputmux {
	CAMIF = 0,
	TESTGEN,
	EXTERNAL_READ
};

struct msm_vfe_camif_cfg {
	uint32_t lines_per_frame;
	uint32_t pixels_per_line;
	uint32_t first_pixel;
	uint32_t last_pixel;
	uint32_t first_line;
	uint32_t last_line;
	uint32_t epoch_line0;
	uint32_t epoch_line1;
	uint32_t camif_input;
};

struct msm_vfe_pix_cfg {
	struct msm_vfe_camif_cfg camif_cfg;
	uint32_t input_mux;
	uint32_t pixel_pattern;
};

struct msm_vfe_rdi_cfg {
	uint8_t cid;
	uint8_t frame_based;
};

struct msm_vfe_input_cfg {
	union {
		struct msm_vfe_pix_cfg pix_cfg;
		struct msm_vfe_rdi_cfg rdi_cfg;
	} d;
	uint32_t input_src;
	uint32_t input_pix_clk;
};

struct msm_vfe_axi_stream_request_cmd {
	uint32_t session_id;
	uint32_t stream_id;
	uint32_t output_format;
	uint32_t stream_src;
	struct msm_vfe_axi_plane_cfg plane_cfg[MAX_PLANES_PER_STREAM];
	uint32_t burst_count;
	uint32_t hfr_mode;
	uint8_t frame_base;
	uint32_t init_frame_drop;
	uint32_t frame_skip_pattern;
	uint8_t buf_divert;
	uint32_t axi_stream_handle;
};

struct msm_vfe_axi_stream_cfg_cmd {
	uint8_t num_streams;
	uint32_t stream_handle[MAX_NUM_STREAM];
	uint32_t cmd;
};

struct msm_isp_qbuf_info {
	uint32_t handle;
	int buf_idx;
	/* only used for prepare buffer */
	struct v4l2_buffer buffer;
	/* only used for diverted buffer */
	uint32_t dirty_buf;
};

struct msm_isp_buf_request {
	uint32_t session_id;
	uint32_t stream_id;
	uint8_t num_buf;
	uint32_t handle;
	uint32_t buf_type;
};

/* The ISP in this tree outputs either one plane (Bayer) or two planes
 * (NV12/NV21 semi-planar 4:2:0).  There is no three-plane YUV420:
 * msm_isp_validate_axi_request() rejects it with "Invalid output
 * format".  NV12 it is: plane 0 is Y, plane 1 is the interleaved CbCr. */
#define ISP_PIX_FMT_NV12 0x3231564e	/* 'N','V','1','2' */

#define VIDIOC_MSM_ISP_REQUEST_BUF \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 1, struct msm_isp_buf_request)
#define VIDIOC_MSM_ISP_ENQUEUE_BUF \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 2, struct msm_isp_qbuf_info)
#define VIDIOC_MSM_ISP_RELEASE_BUF \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 3, struct msm_isp_buf_request)
#define VIDIOC_MSM_ISP_REQUEST_STREAM \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 4, struct msm_vfe_axi_stream_request_cmd)
#define VIDIOC_MSM_ISP_CFG_STREAM \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 5, struct msm_vfe_axi_stream_cfg_cmd)
#define VIDIOC_MSM_ISP_RELEASE_STREAM \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 6, struct msm_vfe_axi_stream_release_cmd)
#define VIDIOC_MSM_ISP_INPUT_CFG \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 7, struct msm_vfe_input_cfg)

/* The ISP's own register configuration -- the chromatrix and the rest of
 * the camera2 "cam_config": a list of struct msm_vfe_reg_cfg_cmd blocks
 * plus one data blob (include/media/msmb_isp.h).
 *
 *   struct msm_vfe_cfg_cmd2 {
 *       uint16_t num_cfg;    how many blocks
 *       uint16_t cmd_len;    the length of the data blob, in bytes
 *       void __user *cfg_data;
 *       void __user *cfg_cmd;
 *   };
 *
 * This is the ioctl the vendor camera HAL uses to hand the ISP its
 * configuration; we replay the very same bytes (see cam_config.h). */
struct msm_vfe_cfg_cmd2 {
	unsigned short num_cfg;
	unsigned short cmd_len;
	void *cfg_data;
	void *cfg_cmd;
};
#define VIDIOC_MSM_VFE_REG_CFG \
	_IOWR('V', BASE_VIDIOC_PRIVATE, struct msm_vfe_cfg_cmd2)

/*
 * The camera2 stack tells the ISP that the configuration is complete
 * and that the pixel source is live; without these two the ISP never
 * processes a frame.
 */
#define VIDIOC_MSM_ISP_SET_SRC_STATE \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 8, struct msm_vfe_axi_src_state)
#define VIDIOC_MSM_ISP_CONFIG_DONE \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 14, int)

/*
 * The per-frame "I want a frame now" ioctl (the camera2 HAL sends it
 * once per frame, at the start and at the end of its cycle).
 */
#define VIDIOC_MSM_ISP_UPDATE_STREAM \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 13, \
	      struct msm_vfe_axi_stream_update_cmd)

/* the kernel's own layout (include/media/msmb_isp.h) */
enum msm_vfe_axi_stream_update_type {
	ENABLE_STREAM_BUF_DIVERT,
	DISABLE_STREAM_BUF_DIVERT,
	UPDATE_STREAM_FRAMEDROP_PATTERN,
	UPDATE_STREAM_REQUEST_FRAMES,
};

struct msm_vfe_axi_stream_update_cmd {
	uint32_t stream_handle;
	enum msm_vfe_axi_stream_update_type update_type;
	enum msm_vfe_frame_skip_pattern skip_pattern;
	uint32_t request_frm_num;
};

/* the kernel's own layout (include/media/msmb_isp.h) -- the ioctl
 * number encodes the size, so it has to match exactly */
/* the kernel's own layout (include/media/msmb_isp.h) -- the ioctl
 * number encodes the size, so it has to match exactly.  The input source
 * enum already exists in v4l2_caf.h, so only the struct is added. */
struct msm_vfe_axi_src_state {
	enum msm_vfe_input_src input_src;
	uint32_t src_active;
};

#endif /* ISP_H */
