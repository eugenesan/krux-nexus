/*
 * imaging4.c -- complete the camera handshake on the device.
 *
 * Parent = imaging server:
 *   - open /dev/video0 (the "msm-config" node)
 *   - subscribe to every event id 1..7 (this tree's v4l2_event_subscribed()
 *     matches type AND id exactly -- no id==0 wildcard)
 *   - dequeue events, set the status word, and ACK with CMD_ACK
 *
 * Child = capture client:
 *   - open the rear sensor node, which makes the kernel post NEW_SESSION
 *     and block until we ACK it
 *   - then walk the ordinary V4L2 path
 *
 * ABI NOTE: the kernel running on this device is NOT built from our tree, and
 * its struct v4l2_event is 128 bytes where ours is 136. The ioctl number
 * encodes the struct size, so DQEVENT and CMD_ACK are built with a 128-byte
 * payload here. (Verified by probing every size: only 128 is accepted.)
 * The event payload words are at offset 4: word 0 = command, word 1 = status.
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

#define EVSZ 128
#define DQ_IOCTL  _IOR('V', 89, char[EVSZ])
#define ACK_IOCTL _IOW('V', 224, char[EVSZ])
#define EVTYPE (V4L2_EVENT_PRIVATE_START + 0x2000)

/* offsets inside the 128-byte event */
#define OFF_DATA   4
#define OFF_CMD    4
#define OFF_STATUS 8

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
	case 1: return "S_CROP"; case 2: return "G_CROP"; case 3: return "G_FMT";
	case 4: return "S_FMT"; case 5: return "TRY_FMT"; case 6: return "METADATA";
	case 7: return "QUERY_CAP"; case 8: return "STREAM_ON";
	case 9: return "STREAM_OFF"; case 10: return "NEW_STREAM";
	case 11: return "DEL_STREAM"; case 12: return "SHUTDOWN";
	case 13: return "STREAM_INFO_SYNC";
	default: return "?";
	}
}

static void child(const char *node)
{
	int fd = open(node, O_RDWR);
	if (fd < 0) {
		printf("[child] open(%s): %s\n", node, strerror(errno));
		_exit(1);
	}
	printf("[child] open(%s) SUCCEEDED -- the handshake completed\n", node);

	struct v4l2_capability cap;
	memset(&cap, 0, sizeof(cap));
	if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0)
		printf("[child] QUERYCAP ok: driver=%s card=%s caps=0x%x\n",
		       cap.driver, cap.card, cap.capabilities);
	else
		printf("[child] QUERYCAP: %s\n", strerror(errno));

	struct v4l2_fmtdesc fd_;
	memset(&fd_, 0, sizeof(fd_));
	int n = 0;
	while (ioctl(fd, VIDIOC_ENUM_FMT, &fd_) == 0 && n < 8) {
		printf("[child] fmt[%u] %c%c%c%c  %s\n", fd_.index,
		       fd_.pixelformat & 0xff, (fd_.pixelformat >> 8) & 0xff,
		       (fd_.pixelformat >> 16) & 0xff, (fd_.pixelformat >> 24) & 0xff,
		       fd_.description);
		fd_.index++;
		n++;
	}
	fflush(stdout);

	struct v4l2_format fmt;
	memset(&fmt, 0, sizeof(fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	fmt.fmt.pix_mp.width = 640;
	fmt.fmt.pix_mp.height = 480;
	fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_YUV420;
	fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;
	fmt.fmt.pix_mp.num_planes = 3;
	if (ioctl(fd, VIDIOC_S_FMT, &fmt) == 0)
		printf("[child] S_FMT ok: %ux%u fmt=%c%c%c%c planes=%u\n",
		       fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height,
		       fmt.fmt.pix_mp.pixelformat & 0xff,
		       (fmt.fmt.pix_mp.pixelformat >> 8) & 0xff,
		       (fmt.fmt.pix_mp.pixelformat >> 16) & 0xff,
		       (fmt.fmt.pix_mp.pixelformat >> 24) & 0xff,
		       fmt.fmt.pix_mp.num_planes);
	else
		printf("[child] S_FMT: %s\n", strerror(errno));
	fflush(stdout);

	struct v4l2_requestbuffers rb;
	memset(&rb, 0, sizeof(rb));
	rb.count = 4;
	rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	rb.memory = V4L2_MEMORY_MMAP;
	if (ioctl(fd, VIDIOC_REQBUFS, &rb) == 0)
		printf("[child] REQBUFS ok: %u buffers\n", rb.count);
	else
		printf("[child] REQBUFS: %s\n", strerror(errno));
	fflush(stdout);
	_exit(0);
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	int seconds = (argc > 1) ? atoi(argv[1]) : 20;
	const char *node = (argc > 2) ? argv[2] : "/dev/video1";

	int fd = open("/dev/video0", O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		printf("open /dev/video0: %s\n", strerror(errno));
		return 1;
	}
	for (int id = 1; id <= 7; id++) {
		struct v4l2_event_subscription sub;
		memset(&sub, 0, sizeof(sub));
		sub.type = EVTYPE;
		sub.id = id;
		if (ioctl(fd, VIDIOC_SUBSCRIBE_EVENT, &sub) < 0)
			printf("SUBSCRIBE id=%d: %s\n", id, strerror(errno));
	}
	printf("[parent] subscribed to event ids 1..7 -- we are the imaging server\n");

	pid_t pid = fork();
	if (pid == 0) {
		close(fd);
		child(node);
		_exit(0);
	}

	int n = 0;
	for (int i = 0; i < seconds * 20; i++) {
		struct pollfd pfd = { .fd = fd, .events = POLLIN };
		if (poll(&pfd, 1, 100) <= 0) {
			int st;
			if (waitpid(pid, &st, WNOHANG) == pid) {
				printf("[parent] child exited\n");
				break;
			}
			continue;
		}
		unsigned char buf[EVSZ];
		memset(buf, 0, sizeof(buf));
		if (ioctl(fd, DQ_IOCTL, buf) < 0) {
			printf("[parent] DQEVENT: %s\n", strerror(errno));
			break;
		}
		unsigned cmd = rd32(buf + OFF_CMD);
		printf("[parent] event cmd=%u (%s) status=%u session=0x%x stream=%u\n",
		       cmd, cmd_name(cmd), rd32(buf + OFF_STATUS),
		       rd32(buf + OFF_DATA + 8), rd32(buf + OFF_DATA + 12));
		fflush(stdout);

		wr32(buf + OFF_STATUS, 1 /* MSM_CAMERA_CMD_SUCESS */);
		if (ioctl(fd, ACK_IOCTL, buf) < 0)
			printf("[parent]   ACK failed: %s\n", strerror(errno));
		else
			printf("[parent]   ACKed\n");
		fflush(stdout);
		n++;
	}
	printf("[parent] %d events serviced\n", n);
	return 0;
}
