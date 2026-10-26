/*
 * imaging6.c -- our own imaging server for the Nexus 5 (hammerhead) camera.
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
 * TRAP 1: this tree's v4l2_event_subscribed() matches type AND id exactly --
 * there is no "id == 0 means all" wildcard, so ids 1..7 have to be
 * subscribed one by one or the kernel waits forever.
 *
 * TRAP 2: the ioctl number encodes the size of struct v4l2_event, and the
 * kernel's struct is NOT the same as the host's: the union inside it and
 * the kernel's 64-bit struct timespec make it 128 bytes, where the host's
 * /usr/include/linux/videodev2.h gives 136.  Probing every size on the
 * device (standalone/cam/dqprobe.c) shows that only 128 is accepted --
 * everything else is EINVAL.  So the event is handled here as a raw byte
 * buffer of exactly 128 bytes.
 *
 * Build:
 *   arm-linux-gnueabihf-gcc -O2 -static -o imaging6 imaging6.c
 * Run (needs root):
 *   adb shell /data/local/tmp/imaging6 [seconds] [node]
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

/* --- this tree's constants (include/media/msmb_camera.h) ---------------- */
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

/* commands, in the order the kernel uses them */
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

/* the size the kernel accepts for struct v4l2_event (probed on device) */
#define EVSZ 128
#define DQ_IOCTL  _IOR('V', 89, char[EVSZ])
#define ACK_IOCTL _IOW('V', 224, char[EVSZ])

/*
 * The kernel's struct v4l2_event, as it is actually laid out here (read
 * off a live event; the header in the host's /usr/include is a different
 * one and does not describe it):
 *
 *    0  __u32 type
 *    4  padding (the union is 8-byte aligned)
 *    8  union u { ... __u8 data[64]; }   <- the msm event data starts here
 *         8  command        (word 0)
 *        12  status         (word 1)
 *        16  session_id     (word 2)
 *        20  stream_id      (word 3)
 *        24  map_op         (word 4)
 *        28  map_buf_idx    (word 5)
 *        32  notify         (word 6)
 *        36  arg_value      (word 7)
 *        40  ret_value      (word 8)
 *    72  __u32 pending
 *    76  __u32 sequence
 *    80  timestamp.tv_sec   (32-bit)
 *    84  timestamp.tv_nsec  (32-bit)
 *    88  __u32 id            <- the event id
 *    92  __u32 reserved[8]
 *   124  padding -> 128 bytes
 */
#define OFF_CMD     8
#define OFF_STATUS  12
#define OFF_SESSION 16
#define OFF_STREAM  20
#define OFF_ARG     36
#define OFF_RET     40
#define OFF_ID      88

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

/* --- the capture client ---------------------------------------------- */
static void child(const char *node)
{
	int fd = open(node, O_RDWR);

	if (fd < 0) {
		printf("[child] open(%s): %s\n", node, strerror(errno));
		_exit(1);
	}
	printf("[child] open(%s) SUCCEEDED -- the handshake completed\n", node);
	fflush(stdout);
	_exit(0);
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	int seconds = (argc > 1) ? atoi(argv[1]) : 20;
	const char *node = (argc > 2) ? argv[2] : "/dev/video1";
	unsigned char buf[EVSZ];
	int fd, n = 0;

	fd = open("/dev/video0", O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		printf("[parent] open /dev/video0: %s\n", strerror(errno));
		return 1;
	}
	for (unsigned id = MSM_CAMERA_NEW_SESSION; id <= MSM_CAMERA_MSM_NOTIFY; id++) {
		struct { unsigned int type, id, flags, reserved[5]; } sub;
		memset(&sub, 0, sizeof(sub));
		sub.type = MSM_CAMERA_V4L2_EVENT_TYPE;
		sub.id = id;
		/* VIDIOC_SUBSCRIBE_EVENT = _IOW('V', 90, struct v4l2_event_subscription) */
		if (ioctl(fd, _IOW('V', 90, char[32]), &sub) < 0)
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
		memset(buf, 0, sizeof(buf));
		if (ioctl(fd, DQ_IOCTL, buf) < 0) {
			printf("[parent] DQEVENT: %s\n", strerror(errno));
			break;
		}
		unsigned cmd = rd32(buf + OFF_CMD);
		printf("[parent] event: id=%u type=0x%08x cmd=0x%08x(%s) status=%u"
		       " session=%u stream=%u arg=0x%x\n",
		       rd32(buf + OFF_ID), rd32(buf), cmd, cmd_name(cmd),
		       rd32(buf + OFF_STATUS), rd32(buf + OFF_SESSION),
		       rd32(buf + OFF_STREAM), rd32(buf + OFF_ARG));
		fflush(stdout);

		wr32(buf + OFF_STATUS, MSM_CAMERA_CMD_SUCESS);
		wr32(buf + OFF_RET, 0);
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
