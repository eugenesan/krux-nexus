/*
 * imaging5.c -- our own imaging server for the Nexus 5 (hammerhead) camera.
 *
 * The CAF camera_v2 stack has no userspace of its own.  Every V4L2 request
 * on a sensor node is posted as an event to a userspace "imaging server"
 * (on Android: /vendor/bin/mm-qcamera-daemon) and the kernel blocks until
 * that server acknowledges it:
 *
 *   drivers/media/platform/msm/camera_v2/msm.c
 *       int msm_post_event(struct v4l2_event *event, int timeout)
 *       {
 *               if (!atomic_read(&serv_running)) {
 *                       pr_info("%s: daemon hasn't subscribed yet!\n", __func__);
 *                       return -EIO;
 *               }
 *               ...
 *       }
 *
 * So without a server, opening the rear sensor node fails with EIO.  This
 * program IS that server.
 *
 * The protocol (all of it in this tree, include/media/msmb_camera.h):
 *
 *   - the server opens /dev/video0, the "msm-config" node
 *   - it subscribes to the event type MSM_CAMERA_V4L2_EVENT_TYPE
 *     (V4L2_EVENT_PRIVATE_START + 0x2000) for event ids 1..7
 *   - the kernel posts a struct v4l2_event whose u.data[] holds a
 *     struct msm_v4l2_event_data: word 0 = command, word 1 = status,
 *     word 2 = session id, word 3 = stream id, word 7 = arg_value,
 *     word 8 = ret_value
 *   - the server does the work for that command (on the subdevices) and
 *     acknowledges with MSM_CAM_V4L2_IOCTL_CMD_ACK
 *     (_IOW('V', 224, struct v4l2_event)) on the same fd, echoing the
 *     event back with status = MSM_CAMERA_CMD_SUCESS and ret_value set.
 *
 * TRAP: this tree's v4l2_event_subscribed() matches type AND id exactly --
 * there is no "id == 0 means all" wildcard, so ids 1..7 have to be
 * subscribed one by one or the kernel waits forever.
 *
 * TRAP: the ioctl number encodes the size of struct v4l2_event, and that
 * size differs between kernels (128 bytes in the ROM's kernel, 120 in
 * ours).  Both ioctls below are therefore built from the real struct, not
 * from a hard-coded number.
 *
 * Build:
 *   arm-linux-gnueabihf-gcc -O2 -static -o imaging5 imaging5.c
 * Run (needs root):
 *   adb shell /data/local/tmp/imaging5 [seconds] [node]
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

/* --- this tree's constants (include/media/msmb_camera.h) ---------------- */
#define MSM_CAMERA_V4L2_EVENT_TYPE (V4L2_EVENT_PRIVATE_START + 0x00002000)
#define MSM_CAMERA_EVENT_MIN       0
#define MSM_CAMERA_NEW_SESSION     (MSM_CAMERA_EVENT_MIN + 1)
#define MSM_CAMERA_DEL_SESSION     (MSM_CAMERA_EVENT_MIN + 2)
#define MSM_CAMERA_SET_PARM        (MSM_CAMERA_EVENT_MIN + 3)
#define MSM_CAMERA_GET_PARM        (MSM_CAMERA_EVENT_MIN + 4)
#define MSM_CAMERA_MAPPING_CFG     (MSM_CAMERA_EVENT_MIN + 5)
#define MSM_CAMERA_MAPPING_SES     (MSM_CAMERA_EVENT_MIN + 6)
#define MSM_CAMERA_MSM_NOTIFY      (MSM_CAMERA_EVENT_MIN + 7)

#define MSM_CAMERA_CMD_SUCESS      0x00000001

/* commands, in the order the kernel uses them */
#define PRIV_S_CROP        (V4L2_CID_PRIVATE_BASE + 1)
#define PRIV_G_CROP        (V4L2_CID_PRIVATE_BASE + 2)
#define PRIV_G_FMT         (V4L2_CID_PRIVATE_BASE + 3)
#define PRIV_S_FMT         (V4L2_CID_PRIVATE_BASE + 4)
#define PRIV_TRY_FMT       (V4L2_CID_PRIVATE_BASE + 5)
#define PRIV_METADATA      (V4L2_CID_PRIVATE_BASE + 6)
#define PRIV_QUERY_CAP     (V4L2_CID_PRIVATE_BASE + 7)
#define PRIV_STREAM_ON     (V4L2_CID_PRIVATE_BASE + 8)
#define PRIV_STREAM_OFF    (V4L2_CID_PRIVATE_BASE + 9)
#define PRIV_NEW_STREAM    (V4L2_CID_PRIVATE_BASE + 10)
#define PRIV_DEL_STREAM    (V4L2_CID_PRIVATE_BASE + 11)
#define PRIV_SHUTDOWN      (V4L2_CID_PRIVATE_BASE + 12)
#define PRIV_STREAM_INFO_SYNC (V4L2_CID_PRIVATE_BASE + 13)

/*
 * The 32-bit layout of struct v4l2_event, spelled out.  The host's
 * /usr/include/linux/videodev2.h is the 64-bit one (struct timespec is
 * 16 bytes there), and the ioctl number encodes the size, so using the
 * host's struct gives the wrong ioctl and the kernel answers EINVAL.
 * This tree's struct is 120 bytes:
 *
 *   0  __u32 type
 *   4  union { ... __u8 data[64]; } u
 *  68  __u32 pending
 *  72  __u32 sequence
 *  76  struct timespec timestamp   (the kernel's is 64-bit: 16 bytes)
 *  92  __u32 id
 *  96  __u32 reserved[8]
 *
 * 128 bytes in total.  (The userspace timespec is 32-bit, so the host's
 * /usr/include/linux/videodev2.h gives 136 bytes and the wrong ioctl
 * number; probing every size on the device shows that only 128 is
 * accepted -- anything else is EINVAL.)
 */
struct v4l2_event32 {
	unsigned int type;
	unsigned char u[64];
	unsigned int pending;
	unsigned int sequence;
	long long tv_sec;	/* the kernel's timespec is 64-bit */
	long long tv_nsec;
	unsigned int id;
	unsigned int reserved[8];
};

#define DQ_IOCTL  _IOR('V', 89, struct v4l2_event32)
#define ACK_IOCTL _IOW('V', 224, struct v4l2_event32)

/* the event payload is event->u.data[]; word 0 = command */
#define OFF_ID      92
#define OFF_CMD     4
#define OFF_STATUS  8
#define OFF_SESSION 12
#define OFF_STREAM  16
#define OFF_ARG     32
#define OFF_RET     36

static unsigned rd32(const void *p)
{
	const unsigned char *b = p;
	return b[0] | (b[1] << 8) | (b[2] << 16) | ((unsigned)b[3] << 24);
}
static void wr32(void *p, unsigned v)
{
	unsigned char *b = p;
	b[0] = v; b[1] = v >> 8; b[2] = v >> 16; b[3] = v >> 24;
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

static const char *evt_name(unsigned id)
{
	switch (id) {
	case MSM_CAMERA_NEW_SESSION: return "NEW_SESSION";
	case MSM_CAMERA_DEL_SESSION: return "DEL_SESSION";
	case MSM_CAMERA_SET_PARM: return "SET_PARM";
	case MSM_CAMERA_GET_PARM: return "GET_PARM";
	case MSM_CAMERA_MAPPING_CFG: return "MAPPING_CFG";
	case MSM_CAMERA_MAPPING_SES: return "MAPPING_SES";
	case MSM_CAMERA_MSM_NOTIFY: return "MSM_NOTIFY";
	default: return "?";
	}
}

/* --- the capture client ---------------------------------------------- */
static void child(const char *node)
{
	struct v4l2_capability cap;
	struct v4l2_format fmt;
	struct v4l2_requestbuffers rb;
	int fd = open(node, O_RDWR);

	if (fd < 0) {
		printf("[child] open(%s): %s\n", node, strerror(errno));
		_exit(1);
	}
	printf("[child] open(%s) SUCCEEDED -- the handshake completed\n", node);
	fflush(stdout);

	memset(&cap, 0, sizeof(cap));
	if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0)
		printf("[child] QUERYCAP: driver=%s card=%s caps=0x%llx\n",
		       cap.driver, cap.card, (unsigned long long)cap.capabilities);
	else
		printf("[child] QUERYCAP: %s\n", strerror(errno));
	fflush(stdout);

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
	int fd;

	printf("[parent] sizeof(struct v4l2_event32) = %zu (host's is %zu)\n",
	       sizeof(struct v4l2_event32), sizeof(struct v4l2_event));

	fd = open("/dev/video0", O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		printf("[parent] open /dev/video0: %s\n", strerror(errno));
		return 1;
	}
	for (unsigned id = MSM_CAMERA_NEW_SESSION; id <= MSM_CAMERA_MSM_NOTIFY; id++) {
		struct v4l2_event_subscription sub;
		memset(&sub, 0, sizeof(sub));
		sub.type = MSM_CAMERA_V4L2_EVENT_TYPE;
		sub.id = id;
		if (ioctl(fd, VIDIOC_SUBSCRIBE_EVENT, &sub) < 0)
			printf("[parent] SUBSCRIBE id=%u: %s\n", id, strerror(errno));
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
		if (poll(&pfd, 1, 100) <= 0) {
			int st;
			if (waitpid(pid, &st, WNOHANG) == pid) {
				printf("[parent] child exited\n");
				break;
			}
			continue;
		}
		struct v4l2_event32 buf;
		memset(&buf, 0, sizeof(buf));
		if (ioctl(fd, DQ_IOCTL, &buf) < 0) {
			printf("[parent] DQEVENT: %s\n", strerror(errno));
			break;
		}
		unsigned type = buf.type;
		unsigned id = buf.id;
		unsigned cmd = rd32((unsigned char *)&buf + OFF_CMD);
		printf("[parent] event type=0x%x id=%u(%s) cmd=%u(%s) status=%u"
		       " session=0x%x stream=%u arg=%u\n",
		       type, id, evt_name(id), cmd, cmd_name(cmd),
		       rd32((unsigned char *)&buf + OFF_STATUS),
		       rd32((unsigned char *)&buf + OFF_SESSION),
		       rd32((unsigned char *)&buf + OFF_STREAM),
		       rd32((unsigned char *)&buf + OFF_ARG));
		fflush(stdout);

		/* do the work for this command, then acknowledge */
		wr32((unsigned char *)&buf + OFF_STATUS, MSM_CAMERA_CMD_SUCESS);
		wr32((unsigned char *)&buf + OFF_RET, 0);
		if (ioctl(fd, ACK_IOCTL, &buf) < 0)
			printf("[parent]   ACK failed: %s\n", strerror(errno));
		else
			printf("[parent]   ACKed\n");
		fflush(stdout);
		n++;
	}
	printf("[parent] %d events serviced\n", n);
	return 0;
}
