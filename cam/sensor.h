/*
 * sensor.h -- the sensor-config ioctl interface, for userspace.
 *
 * The sensor's register settings are handed to the kernel with
 * VIDIOC_MSM_SENSOR_CFG (include/media/msm_cam_sensor.h):
 *
 *      case CFG_WRITE_I2C_ARRAY: {
 *              struct msm_camera_i2c_reg_setting conf_array;
 *              struct msm_camera_i2c_reg_array *reg_setting = NULL;
 *              ...
 *              copy_from_user(reg_setting, conf_array.reg_setting,
 *                             conf_array.size * sizeof(struct msm_camera_i2c_reg_array));
 *
 * so the arrays the user passes are exactly
 * struct msm_camera_i2c_reg_array { u16 addr; u16 data; u32 delay; }.
 */
#ifndef SENSOR_H
#define SENSOR_H

#include <stdint.h>

#define BASE_VIDIOC_PRIVATE 192

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

struct msm_camera_i2c_reg_array {
	unsigned short reg_addr;
	unsigned short reg_data;
	unsigned int delay;
};

struct msm_camera_i2c_reg_setting {
	struct msm_camera_i2c_reg_array *reg_setting;
	unsigned short size;
	enum msm_camera_i2c_reg_addr_type addr_type;
	enum msm_camera_i2c_data_type data_type;
	unsigned short delay;
};

#endif /* SENSOR_H */
