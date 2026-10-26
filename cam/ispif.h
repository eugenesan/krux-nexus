/*
 * ispif.h -- the ISP interface (the piece between the CSID and the ISP).
 *
 * This kernel's own header (include/media/msmb_ispif.h) has to be
 * mirrored exactly: the ioctl number carries the size of the struct,
 * and the kernel's and the camera2 HAL's structs are both 368 bytes --
 * which is what the HAL's ioctl number said when we captured it.
 */

#ifndef ISPIF_H
#define ISPIF_H

#include <stdint.h>

#define BASE_VIDIOC_PRIVATE 192

/* the CSID's hardware versions */
#define CSID_VERSION_V20	0x02000011
#define CSID_VERSION_V22	0x02001000
#define CSID_VERSION_V30	0x30000000

enum msm_ispif_vfe_intf {
	VFE0,
	VFE1,
	VFE_MAX
};

enum msm_ispif_intftype {
	PIX0,
	RDI0,
	PIX1,
	RDI1,
	RDI2,
	INTF_MAX
};

#define MAX_PARAM_ENTRIES (INTF_MAX * 2)

enum msm_ispif_cid {
	CID0,
	CID1,
	CID2,
	CID3,
	CID4,
	CID5,
	CID6,
	CID7,
	CID8,
	CID9,
	CID10,
	CID11,
	CID12,
	CID13,
	CID14,
	CID15,
	CID_MAX
};

enum msm_ispif_csid {
	CSID0,
	CSID1,
	CSID2,
	CSID3,
	CSID_MAX
};

struct msm_isp_info {
	uint32_t max_resolution;
	uint32_t id;
	uint32_t ver;
};

struct msm_ispif_vfe_info {
	int num_vfe;
	struct msm_isp_info info[VFE_MAX];
};

struct msm_ispif_params_entry {
	enum msm_ispif_vfe_intf vfe_intf;
	enum msm_ispif_intftype intftype;
	int num_cids;
	enum msm_ispif_cid cids[3];
	enum msm_ispif_csid csid;
	int crop_enable;
	uint16_t crop_start_pixel;
	uint16_t crop_end_pixel;
};

struct msm_ispif_param_data {
	uint32_t num;
	struct msm_ispif_params_entry entries[MAX_PARAM_ENTRIES];
};

enum ispif_cfg_type_t {
	ISPIF_CLK_ENABLE,
	ISPIF_CLK_DISABLE,
	ISPIF_INIT,
	ISPIF_CFG,
	ISPIF_START_FRAME_BOUNDARY,
	ISPIF_STOP_FRAME_BOUNDARY,
	ISPIF_STOP_IMMEDIATELY,
	ISPIF_RELEASE,
	ISPIF_ENABLE_REG_DUMP,
	ISPIF_SET_VFE_INFO,
};

struct ispif_cfg_data {
	enum ispif_cfg_type_t cfg_type;
	union {
		int reg_dump;			/* ISPIF_ENABLE_REG_DUMP */
		uint32_t csid_version;		/* ISPIF_INIT */
		struct msm_ispif_vfe_info vfe_info;	/* ISPIF_SET_VFE_INFO */
		struct msm_ispif_param_data params;	/* CFG, START, STOP */
	};
};

#define VIDIOC_MSM_ISPIF_CFG \
	_IOWR('V', BASE_VIDIOC_PRIVATE, struct ispif_cfg_data)

/* the ISP-IF's subdev: /dev/v4l-subdev15 on this device ("msm_ispif") */
#define ISPIF_NODE "/dev/v4l-subdev15"

/*
 * The CPP (the colour processor between the ISP and memory).  The
 * camera2 HAL hands it the chromatrix as a frame message; without it
 * the ISP's output never leaves the CPP.
 */
struct msm_cpp_buffer_info_t {
	int fd;
	uint32_t index;
	uint32_t offset;
	uint8_t native_buff;
	uint8_t processed_divert;
};

struct msm_cpp_stream_buff_info_t {
	uint32_t identity;
	uint32_t num_buffs;
	struct msm_cpp_buffer_info_t *buffer_info;
};

struct msm_cpp_frame_info_t {
	int32_t frame_id;
	long timestamp_sec;
	long timestamp_usec;
	uint32_t inst_id;
	uint32_t identity;
	uint32_t client_id;
	int32_t frame_type;		/* 0 = offline, 1 = realtime */
	uint32_t num_strips;
	struct msm_cpp_frame_strip_info *strip_info;
	uint32_t msg_len;
	uint32_t *cpp_cmd_msg;
	int src_fd;
	int dst_fd;
	void *src_ion_handle;
	void *dest_ion_handle;
	long in_timestamp_sec;
	long in_timestamp_usec;
	long out_timestamp_sec;
	long out_timestamp_usec;
	void *cookie;
	int32_t *status;
	int32_t duplicate_output;
	uint32_t duplicate_identity;
	struct msm_cpp_buffer_info_t input_buffer_info;
	struct msm_cpp_buffer_info_t output_buffer_info[2];
};

/* The CPP's ioctls, as the camera_v2 driver has them
 * (include/media/msmb_pproc.h).  The numbering is *not* the one in
 * include/media/msm_camera.h: that header is the camera_v1 one, and
 * its BASE+1 is GET_EVENTPAYLOAD here, not APPEND. */
#define VIDIOC_MSM_CPP_CFG \
	_IOWR('V', BASE_VIDIOC_PRIVATE, struct msm_camera_v4l2_ioctl_t)
#define VIDIOC_MSM_CPP_GET_EVENTPAYLOAD \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 1, struct msm_camera_v4l2_ioctl_t)
#define VIDIOC_MSM_CPP_LOAD_FIRMWARE \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 3, struct msm_camera_v4l2_ioctl_t)
#define VIDIOC_MSM_CPP_ENQUEUE_STREAM_BUFF_INFO \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 6, struct msm_camera_v4l2_ioctl_t)
#define VIDIOC_MSM_CPP_DEQUEUE_STREAM_BUFF_INFO \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 7, struct msm_camera_v4l2_ioctl_t)
#define VIDIOC_MSM_CPP_APPEND_STREAM_BUFF_INFO \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 17, struct msm_camera_v4l2_ioctl_t)

#endif /* ISPIF_H */
