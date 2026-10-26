/*
 * sensor_live.h -- the IMX179's registers as the *vendor's* camera leaves
 * them.
 *
 * These are not guessed and not captured from an old HAL: they were read
 * out of the sensor *while the ROM's own camera app had it open*, on the
 * ROM's 20260926 kernel, with standalone/cam/sensordump:
 *
 *   adb shell su -c "/data/local/tmp/sensordump /dev/v4l-subdev11"
 *
 * The reading taken two seconds after the shutter is the *photo* mode
 * (the preview's mode differs in 0x0203, 0x0205, 0x0340 and 0x0341).
 *
 * Two things make this different from the captured arrays in
 * sensor_config.h:
 *
 *  - the values are *bytes*.  The vendor writes 0x0340 and 0x0341 as
 *    two separate byte writes (0x09 and 0xd6), so the geometry ends up
 *    big-endian in the register pair.  Replaying the captured arrays
 *    wrote 0x0340 = 0x09d6 as one 16-bit word, which put the bytes the
 *    other way round: the sensor then ran in the wrong mode and never
 *    sent the image.
 *
 *  - the PLL is different: 0x0202/0x0203 = 0x18/0x12 in the photo mode
 *    (the captured arrays have 0x09/0xc4).  The PLL decides the sensor's
 *    output rate, and with the wrong one the CSI-2 link never locks.
 *
 * The list also holds registers that the captured arrays do not have at
 * all (0x0304, 0x0306, 0x0407), which shows that the capture was taken
 * from an older camera stack than the one the ROM runs now.
 *
 * The stream-on (0x0100 = 1) is the last entry, as it must be.
 */
#ifndef SENSOR_LIVE_H
#define SENSOR_LIVE_H

/* the vendor's photo mode, read from the sensor */
static const struct imx179_reg imx179_live_photo[] = {
	/* the stream is off while the mode is set up */
	{ 0x0100, 0x00, 0 },
	{ 0x0101, 0x00, 0 },
	{ 0x0102, 0x00, 0 },
	{ 0x0105, 0x01, 0 },
	{ 0x0106, 0x00, 0 },

	/* the PLL: the sensor's output rate */
	{ 0x0202, 0x18, 0 },
	{ 0x0203, 0x12, 0 },
	{ 0x0204, 0x00, 0 },
	{ 0x0205, 0x34, 0 },

	/* the MIPI settings */
	{ 0x0301, 0x05, 0 },
	{ 0x0302, 0x00, 0 },
	{ 0x0303, 0x01, 0 },
	{ 0x0304, 0x0b, 0 },
	{ 0x0305, 0x06, 0 },
	{ 0x0306, 0x02, 0 },
	{ 0x0307, 0xbc, 0 },
	{ 0x0308, 0x00, 0 },
	{ 0x0309, 0x05, 0 },
	{ 0x030a, 0x00, 0 },
	{ 0x030b, 0x01, 0 },
	{ 0x030c, 0x00, 0 },
	{ 0x030d, 0xca, 0 },

	/*
	 * The geometry is NOT here: it is written as a 16-bit word, in
	 * imx179_live_geometry below.  The data type belongs to the
	 * whole array, so the geometry needs an array of its own.
	 */
	{ 0x0344, 0x00, 0 },
	{ 0x0345, 0x00, 0 },
	{ 0x0346, 0x00, 0 },
	{ 0x0347, 0x00, 0 },
	{ 0x0348, 0x0c, 0 },
	{ 0x0349, 0xcf, 0 },
	{ 0x034a, 0x09, 0 },
	{ 0x034b, 0x9f, 0 },
	{ 0x034c, 0x0c, 0 },
	{ 0x034d, 0xd0, 0 },
	{ 0x034e, 0x09, 0 },
	{ 0x034f, 0xa0, 0 },
	{ 0x0350, 0x00, 0 },
	{ 0x0351, 0x00, 0 },

	/* the data format and the black level */
	{ 0x0401, 0x00, 0 },
	{ 0x0405, 0x10, 0 },
	{ 0x0406, 0x00, 0 },
	{ 0x0407, 0x10, 0 },

	/* back to page 0: the mode registers live there */
	{ 0x098e, 0x00, 0 },
	{ 0x0990, 0x00, 0 },

	/* the virtual channel the sensor sends on */
	{ 0x3302, 0x01, 0 },
	{ 0x3303, 0x00, 0 },

	/* the copy of the cropping registers */
	{ 0x33d4, 0x0c, 0 },
	{ 0x33d5, 0xd0, 0 },
	{ 0x33d6, 0x09, 0 },
	{ 0x33d7, 0xa0, 0 },

	/*
	 * The stream-on is NOT here.
	 *
	 * The vendor's order is: the mode is programmed while the sensor
	 * is idle (0x0100 = 0), and the stream is started later, with
	 * PRIV_STREAM_ON -- which the camera2 stack sends only after the
	 * ISP has been configured and its buffers are enqueued.
	 *
	 * That order matters.  In the photo mode the IMX179 sends one
	 * frame and then stops, so a stream-on that comes too early
	 * spends the only frame before the pipeline is ready.  The
	 * imaging server's PRIV_STREAM_ON handler does the stream-on at
	 * the right moment.
	 */
};

#define IMX179_LIVE_PHOTO_LEN \
	(sizeof(imx179_live_photo) / sizeof(imx179_live_photo[0]))

/*
 * The geometry, written as 16-bit words.
 *
 * The vendor's camera HAL writes the geometry (0x0340 and 0x0342) as
 * one word each -- its own ioctl headers say so: the array with the
 * geometry has data_type = MSM_CAMERA_I2C_WORD_DATA, every other
 * array has MSM_CAMERA_I2C_BYTE_DATA.  The sensor's register pairs are
 * big endian, so 0x0340 holds VMAX = 0x09d6 and 0x0342 holds
 * HMAX = 0x0d70; a byte write of the low byte only would leave the
 * high byte at the previous mode's value.
 */
static const struct imx179_reg imx179_live_geometry[] = {
	{ 0x0340, 0x09d6, 0 },
	{ 0x0342, 0x0d70, 0 },
};

#define IMX179_LIVE_GEOMETRY_LEN \
	(sizeof(imx179_live_geometry) / sizeof(imx179_live_geometry[0]))

#endif /* SENSOR_LIVE_H */
