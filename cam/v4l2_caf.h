/*
 * v4l2_caf.h -- the V4L2 ABI of *this* kernel, for userspace.
 *
 * The ioctl number of every V4L2 ioctl encodes the size of the struct it
 * passes, so userspace has to use the kernel's struct layouts, not the
 * host's.  The host's /usr/include/linux/videodev2.h is the uapi header of
 * a different kernel vintage and disagrees with ours in several places:
 *
 *   - struct timeval: the kernel's is two longs (8 bytes on 32-bit), the
 *     host's is the glibc one; that alone makes struct v4l2_buffer 72
 *     bytes here and 80 in the host's header, i.e. a different ioctl
 *     number (VIDIOC_QBUF then returns ENOTTY).
 *   - the multi-planar design is the 2.6.32-era one: struct
 *     v4l2_buffer.m.planes is a POINTER to an array of struct v4l2_plane
 *     and there is one scalar length, not length[].
 *   - struct v4l2_timecode has fields named frames/seconds/minutes/hours.
 *
 * The layouts below are copied from this tree's include/linux/videodev2.h
 * (kernel 3.4, CAF), so the ioctl numbers match the running kernel.
 */
#ifndef V4L2_CAF_H
#define V4L2_CAF_H

#include <stdint.h>
#include <linux/ioctl.h>

typedef uint32_t __u32;
typedef uint16_t __u16;
typedef uint8_t  __u8;

#define V4L2_BUF_TYPE_VIDEO_CAPTURE		1
#define V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE	9
#define V4L2_BUF_TYPE_VIDEO_OUTPUT		2

#define V4L2_MEMORY_MMAP			1
#define V4L2_MEMORY_USERPTR			2
#define V4L2_MEMORY_DMABUF			3

#define V4L2_PIX_FMT_YUV420			0x34565559	/* 'Y','U','V','4' */
#define V4L2_PIX_FMT_NV12			0x3231564e	/* 'N','V','1','2' */

#define V4L2_CAP_VIDEO_CAPTURE			0x00000001
#define V4L2_CAP_STREAMING			0x04000000
#define V4L2_CAP_DEVICE_CAPS			0x80000000

#define V4L2_EVENT_PRIVATE_START		0x08000000

struct caf_timeval {
	long tv_sec;
	long tv_usec;
};

struct v4l2_timecode {
	__u32	type;
	__u32	flags;
	__u8	frames;
	__u8	seconds;
	__u8	minutes;
	__u8	hours;
	__u8	userbits[4];
};

struct v4l2_capability {
	__u8	driver[16];
	__u8	card[32];
	__u8	bus_info[32];
	__u32	version;
	__u32	capabilities;
	__u32	device_caps;
	__u32	reserved[3];
};

struct v4l2_plane {
	__u32	bytesused;
	__u32	length;
	union {
		__u32		mem_offset;
		unsigned long	userptr;
	} m;
	__u32	data_offset;
	__u32	reserved[11];
};

struct v4l2_buffer {
	__u32			index;
	unsigned int		type;		/* enum v4l2_buf_type */
	__u32			bytesused;
	__u32			flags;
	unsigned int		field;		/* enum v4l2_field */
	struct caf_timeval	timestamp;
	struct v4l2_timecode	timecode;
	__u32			sequence;
	unsigned int		memory;		/* enum v4l2_memory */
	union {
		__u32		offset;
		unsigned long	userptr;
		struct v4l2_plane *planes;
	} m;
	__u32			length;
	__u32			input;
	__u32			reserved;
};

struct v4l2_requestbuffers {
	__u32			count;
	unsigned int		type;		/* enum v4l2_buf_type */
	unsigned int		memory;		/* enum v4l2_memory */
	__u32			reserved[2];
};

struct v4l2_format {
	unsigned int	type;		/* enum v4l2_buf_type */
	union {
		__u8	raw_data[200];
	} fmt;
};

struct v4l2_fract {
	__u32	numerator;
	__u32	denominator;
};

struct v4l2_captureparm {
	__u32	capability;
	__u32	capturemode;
	struct v4l2_fract timeperframe;
	__u32	extendedmode;
	__u32	readbuffers;
	__u32	reserved[4];
};

struct v4l2_streamparm {
	unsigned int	type;		/* enum v4l2_buf_type */
	union {
		struct v4l2_captureparm capture;
		__u8	raw_data[200];
	} parm;
};

struct v4l2_event_subscription {
	__u32	type;
	__u32	id;
	__u32	flags;
	__u32	reserved[5];
};

#define VIDIOC_QUERYCAP		_IOR('V',  0, struct v4l2_capability)
#define VIDIOC_G_FMT		_IOWR('V',  4, struct v4l2_format)
#define VIDIOC_S_FMT		_IOWR('V',  5, struct v4l2_format)
#define VIDIOC_REQBUFS		_IOWR('V',  8, struct v4l2_requestbuffers)
#define VIDIOC_QUERYBUF		_IOWR('V',  9, struct v4l2_buffer)
#define VIDIOC_QBUF		_IOWR('V', 15, struct v4l2_buffer)
#define VIDIOC_DQBUF		_IOWR('V', 17, struct v4l2_buffer)
#define VIDIOC_G_PARM		_IOWR('V', 21, struct v4l2_streamparm)
#define VIDIOC_S_PARM		_IOWR('V', 22, struct v4l2_streamparm)
#define VIDIOC_STREAMON		_IOW('V', 18, int)
#define VIDIOC_STREAMOFF	_IOW('V', 19, int)
#define VIDIOC_SUBSCRIBE_EVENT	_IOW('V', 90, struct v4l2_event_subscription)

#endif /* V4L2_CAF_H */
