/*
 * mipi.h -- the CSID/CSIPHY configuration, for userspace.
 *
 * In camera_v2 nothing in the kernel brings up the MIPI link: the
 * imaging server has to configure the CSIPHY and the CSID itself,
 * through their own subdevice ioctls (camera_v2/sensor/csid/msm_csid.c,
 * camera_v2/sensor/csiphy/msm_csiphy.c):
 *
 *   VIDIOC_MSM_CSIPHY_IO_CFG   the lane layout of the PHY
 *   VIDIOC_MSM_CSID_IO_CFG     the lane layout and the VC lookup table
 *
 * The numbers come from the device tree, not from a vendor blob: the
 * imx179 node has
 *
 *      qcom,csi-lane-assign = <0x4320>;
 *      qcom,csi-lane-mask  = <0x1F>;
 *
 * i.e. four lanes, assigned in the order D,C,B,A.
 */
#ifndef MIPI_H
#define MIPI_H

#include <stdint.h>

#define BASE_VIDIOC_PRIVATE 192

#define MAX_CID 16

enum csid_cfg_type_t {
	CSID_INIT = 0,
	CSID_CFG,
	CSID_RELEASE
};

enum csiphy_cfg_type_t {
	CSIPHY_INIT = 0,
	CSIPHY_CFG,
	CSIPHY_RELEASE
};

struct msm_camera_csid_vc_cfg {
	uint8_t cid;
	uint8_t dt;
	uint8_t decode_format;
};

struct msm_camera_csid_lut_params {
	uint8_t num_cid;
	struct msm_camera_csid_vc_cfg *vc_cfg[MAX_CID];
	uint32_t vc_cfg_size;
};

struct msm_camera_csid_params {
	uint8_t lane_cnt;
	uint16_t lane_assign;
	uint8_t phy_sel;
	struct msm_camera_csid_lut_params lut_params;
};

struct msm_sensor_csid_cfg_params {
	struct msm_camera_csid_params *csid_params;
	uint32_t csid_params_size;
};

struct csid_cfg_data {
	uint32_t cfgtype;		/* enum csid_cfg_type_t */
	union {
		uint32_t csid_version;
		struct msm_sensor_csid_cfg_params csid_cfg_params;
	} cfg;
};

struct msm_camera_csiphy_params {
	uint8_t lane_cnt;
	uint8_t settle_cnt;
	uint16_t lane_mask;
	uint8_t combo_mode;
	uint8_t csid_core;
};

struct msm_camera_csi_lane_params {
	uint16_t csi_lane_assign;
	uint16_t csi_lane_mask;
};

struct csiphy_cfg_data {
	uint32_t cfgtype;		/* enum csiphy_cfg_type_t */
	union {
		uint32_t csid_version;
		struct msm_camera_csiphy_params *csiphy_params;
		struct msm_camera_csi_lane_params *csi_lane_params;
	} cfg;
	uint32_t cfg_params_size;
};

#define VIDIOC_MSM_CSIPHY_IO_CFG \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 4, struct csiphy_cfg_data)
#define VIDIOC_MSM_CSID_IO_CFG \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 5, struct csid_cfg_data)

/* the rear camera: csiphy-sd-index 0, csid-sd-index 0 */
#define CSIPHY_NODE "/dev/v4l-subdev0"
#define CSID_NODE   "/dev/v4l-subdev3"

/* from the device tree */
#define CAM_LANE_ASSIGN	0x4320
#define CAM_LANE_MASK	0x1F
#define CAM_LANE_CNT	4

#endif /* MIPI_H */
