/*
 * imaging2.c -- prototype capture client for the Nexus 5 camera.
 *
 * Two processes:
 *
 *   PARENT  the "imaging server". Opens the config node /dev/video0,
 *           subscribes to the private event type (which is what sets the
 *           kernel's serv_running flag), and then services every event the
 *           kernel posts: it prints them and ACKs them with a success
 *           status.
 *
 *   CHILD   the capture client. Opens a sensor node (/dev/video1 = rear
 *           imx179, /dev/video2 = front mt9m114b) and drives ordinary V4L2:
 *           QUERYCAP, ENUM_FMT, S_FMT, REQBUFS, QBUF, STREAMON, DQBUF.
 *           Those ioctls block in the kernel until the imaging server ACKs
 *           the event the kernel posts on our behalf, which is exactly the
 *           handshake this program is demonstrating.
 *
 * If this works, the camera pipeline is reachable from plain userspace with
 * no vendor daemon and no Android userspace.
 *
 * Build:
 *   arm-linux-gnueabihf-gcc -O2 -static -o imaging2 imaging2.c
 * Run (root):
 *   adb shell su -c /data/local/tmp/imaging2 [seconds] [node]
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <linux/videodev2.h>

#define MSM_CAMERA_V4L2_EVENT_TYPE (V4L2_EVENT_PRIVATE_START + 0x00002000)

#define MSM_CAMERA_NEW_SESSION	(0 + 1)
#define MSM_CAMERA_DEL_SESSION	(0 + 2)
#define MSM_CAMERA_SET_PARM	(0 + 3)
#define MSM_CAMERA_GET_PARM	(0 + 4)
#define MSM_CAMERA_MAPPING_CFG	(0 + 5)
#define MSM_CAMERA_MAPPING_SES	(0 + 6)
#define MSM_CAMERA_MSM_NOTIFY	(0 + 7)

#define MSM_CAMERA_PRIV_S_CROP		(0x80000000 + 1)
#define MSM_CAMERA_PRIV_G_CROP		(0x80000000 + 2)
#define MSM_CAMERA_PRIV_G_FMT		(0x80000000 + 3)
#define MSM_CAMERA_PRIV_S_FMT		(0x80000000 + 4)
#define MSM_CAMERA_PRIV_TRY_FMT		(0x80000000 + 5)
#define MSM_CAMERA_PRIV_METADATA	(0x80000000 + 6)
#define MSM_CAMERA_PRIV_QUERY_CAP	(0x80000000 + 7)
#define MSM_CAMERA_PRIV_STREAM_ON	(0x80000000 + 8)
#define MSM_CAMERA_PRIV_STREAM_OFF	(0x80000000 + 9)
#define MSM_CAMERA_PRIV_NEW_STREAM	(0x80000000 + 10)
#define MSM_CAMERA_PRIV_DEL_STREAM	(0x80000000 + 11)
#define MSM_CAMERA_PRIV_SHUTDOWN		(0x80000000 + 12)
#define MSM_CAMERA_PRIV_STREAM_INFO_SYNC (0x80000000 + 13)

#define MSM_CAMERA_CMD_SUCESS 0x00000001
#define BASE_VIDIOC_PRIVATE 192
#define MSM_CAM_V4L2_IOCTL_CMD_ACK \
	_IOW('V', BASE_VIDIOC_PRIVATE + 32, struct v4l2_event)

struct msm_v4l2_event_data {
	unsigned int command, status, session_id, stream_id;
	unsigned int map_op, map_buf_idx, notify, arg_value, ret_value;
	unsigned int nop3, nop4, nop5, nop6, nop7, nop8, nop9;
};

static const char *cmd_name(unsigned int c)
{
	switch (c) {
	case MSM_CAMERA_PRIV_S_CROP:     return "S_CROP";
	case MSM_CAMERA_PRIV_G_CROP:     return "G_CROP";
	case MSM_CAMERA_PRIV_G_FMT:      return "G_FMT";
	case MSM_CAMERA_PRIV_S_FMT:      return "S_FMT";
	case MSM_CAMERA_PRIV_TRY_FMT:    return "TRY_FMT";
	case MSM_CAMERA_PRIV_METADATA:   return "METADATA";
	case MSM_CAMERA_PRIV_QUERY_CAP:  return "QUERY_CAP";
	case MSM_CAMERA_PRIV_STREAM_ON:  return "STREAM_ON";
	case MSM_CAMERA_PRIV_STREAM_OFF: return "STREAM_OFF";
	case MSM_CAMERA_PRIV_NEW_STREAM: return "NEW_STREAM";
	case MSM_CAMERA_PRIV_DEL_STREAM: return "DEL_STREAM";
	case MSM_CAMERA_PRIV_SHUTDOWN:   return "SHUTDOWN";
	case MSM_CAMERA_PRIV_STREAM_INFO_SYNC: return "STREAM_INFO_SYNC";
	default:                         return "?";
	}
}

static const char *evt_name(unsigned int t)
{
	switch (t) {
	case MSM_CAMERA_NEW_SESSION:  return "NEW_SESSION";
	case MSM_CAMERA_DEL_SESSION:  return "DEL_SESSION";
	case MSM_CAMERA_SET_PARM:    return "SET_PARM";
	case MSM_CAMERA_GET_PARM:    return "GET_PARM";
	case MSM_CAMERA_MAPPING_CFG: return "MAPPING_CFG";
	case MSM_CAMERA_MAPPING_SES: return "MAPPING_SES";
	case MSM_CAMERA_MSM_NOTIFY:  return "MSM_NOTIFY";
	default:                     return "?";
	}
}

/* ------------------------------------------------------------------ child */

static void child(const char *node)
{
	int fd = open(node, O_RDWR);
	if (fd < 0) {
		printf("[child] open(%s): %s\n", node, strerror(errno));
		_exit(1);
	}
	printf("[child] opened %s\n", node);

	struct v4l2_capability cap;
	memset(&cap, 0, sizeof(cap));
	if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
		printf("[child] QUERYCAP: %s\n", strerror(errno));
		_exit(1);
	}
	printf("[child] QUERYCAP ok: driver=%s card=%s caps=0x%x\n",
	       cap.driver, cap.card, cap.capabilities);

	struct v4l2_fmtdesc fd_;
	memset(&fd_, 0, sizeof(fd_));
	int n = 0;
	while (ioctl(fd, VIDIOC_ENUM_FMT, &fd_) == 0 && n < 8) {
		printf("[child] fmt[%u] %c%c%c%c %s\n", fd_.index,
		       fd_.pixelformat & 0xff, (fd_.pixelformat >> 8) & 0xff,
		       (fd_.pixelformat >> 16) & 0xff, (fd_.pixelformat >> 24) & 0xff,
		       fd_.description);
		fd_.index++;
		n++;
	}
	fflush(stdout);

	/* Try to configure and stream. */
	struct v4l2_format fmt;
	memset(&fmt, 0, sizeof(fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	fmt.fmt.pix_mp.width = 640;
	fmt.fmt.pix_mp.height = 480;
	fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_YUV420;
	fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;
	fmt.fmt.pix_mp.num_planes = 3;
	if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
		printf("[child] S_FMT: %s\n", strerror(errno));
		_exit(1);
	}
	printf("[child] S_FMT ok: %ux%u fmt=%c%c%c%c planes=%u\n",
	       fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height,
	       fmt.fmt.pix_mp.pixelformat & 0xff,
	       (fmt.fmt.pix_mp.pixelformat >> 8) & 0xff,
	       (fmt.fmt.pix_mp.pixelformat >> 16) & 0xff,
	       (fmt.fmt.pix_mp.pixelformat >> 24) & 0xff,
	       fmt.fmt.pix_mp.num_planes);
	fflush(stdout);

	struct v4l2_requestbuffers rb;
	memset(&rb, 0, sizeof(rb));
	rb.count = 4;
	rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	rb.memory = V4L2_MEMORY_MMAP;
	if (ioctl(fd, VIDIOC_REQBUFS, &rb) < 0) {
		printf("[child] REQBUFS: %s\n", strerror(errno));
		_exit(1);
	}
	printf("[child] REQBUFS ok: %u buffers\n", rb.count);
	fflush(stdout);

	_exit(0);
}

/* ----------------------------------------------------------------- parent */

int main(int argc, char **argv)
{
	/* stdout is a pipe through adb shell, so it is block buffered. Without
	 * this, the child's output is lost when it _exit()s. */
	setvbuf(stdout, NULL, _IONBF, 0);

	int seconds = (argc > 1) ? atoi(argv[1]) : 15;
	const char *node = (argc > 2) ? argv[2] : "/dev/video1";

	int fd = open("/dev/video0", O_RDWR);
	if (fd < 0) {
		printf("open(/dev/video0): %s\n", strerror(errno));
		return 1;
	}
	/*
	 * Subscribe to EVERY event id separately.
	 *
	 * drivers/media/video/v4l2-event.c in this 3.4 tree matches a
	 * subscription with
	 *     if (sev->type == type && sev->id == id)
	 * -- an EXACT id match, with no "id == 0 means all" wildcard (later
	 * kernels added that). The events are posted with event->id = evt_id,
	 * which is one of MSM_CAMERA_NEW_SESSION .. MSM_CAMERA_MSM_NOTIFY
	 * (1..7). A single subscription with id = 0 therefore matches nothing,
	 * and the kernel sits waiting for an ACK that can never arrive.
	 *
	 * MSM_CAM_V4L2_EVENT_TYPE is the same for all of them; only the id
	 * differs.
	 */
	for (int id = MSM_CAMERA_NEW_SESSION; id <= MSM_CAMERA_MSM_NOTIFY; id++) {
		struct v4l2_event_subscription sub;
		memset(&sub, 0, sizeof(sub));
		sub.type = MSM_CAMERA_V4L2_EVENT_TYPE;
		sub.id = id;
		if (ioctl(fd, VIDIOC_SUBSCRIBE_EVENT, &sub) < 0) {
			printf("SUBSCRIBE_EVENT id=%d: %s\n", id, strerror(errno));
			return 1;
		}
	}
	printf("[parent] subscribed to event ids 1..7 -- we are the imaging server\n");
	fflush(stdout);

	pid_t pid = fork();
	if (pid == 0) {
		close(fd);
		child(node);
		_exit(0);
	}

	int n = 0;
	for (int i = 0; i < seconds * 20; i++) {
		struct pollfd pfd = { .fd = fd, .events = POLLIN };
		if (poll(&pfd, 1, 50) <= 0) {
			int st;
			if (waitpid(pid, &st, WNOHANG) == pid)
				break;
			continue;
		}
		/*
		 * DQEVENT needs the type AND the id of the subscription to
		 * pull from: v4l2_event_dequeue() looks the subscription up with
		 * v4l2_event_subscribed(fh, event->type, event->id) and returns
		 * -EINVAL if there is none. A zeroed event therefore fails, so
		 * try each id we subscribed to.
		 */
		struct v4l2_event ev;
		int got = 0;
		for (int id = MSM_CAMERA_NEW_SESSION; id <= MSM_CAMERA_MSM_NOTIFY; id++) {
			memset(&ev, 0, sizeof(ev));
			ev.type = MSM_CAMERA_V4L2_EVENT_TYPE;
			ev.id = id;
			if (ioctl(fd, VIDIOC_DQEVENT, &ev) == 0) {
				got = 1;
				break;
			}
		}
		if (!got) {
			printf("[parent] DQEVENT failed on every id: %s\n", strerror(errno));
			break;
		}
		struct msm_v4l2_event_data *d =
			(struct msm_v4l2_event_data *)&ev.u.data[0];
		printf("[parent] event type=0x%x %-12s cmd=0x%x %-18s status=0x%x "
		       "session=%u stream=%u arg=%u ret=%u\n",
		       ev.type, evt_name(ev.type), d->command, cmd_name(d->command),
		       d->status, d->session_id, d->stream_id, d->arg_value,
		       d->ret_value);
		fflush(stdout);

		d->status = MSM_CAMERA_CMD_SUCESS;
		if (ioctl(fd, MSM_CAM_V4L2_IOCTL_CMD_ACK, &ev) < 0)
			printf("[parent]   ACK failed: %s\n", strerror(errno));
		else
			printf("[parent]   ACKed\n");
		fflush(stdout);
		n++;
	}
	printf("[parent] %d events serviced\n", n);
	return 0;
}
