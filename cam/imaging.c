/*
 * imaging.c -- a prototype replacement for the CAF camera "imaging server".
 *
 * The camera_v2 stack in CAF does not answer V4L2 requests in the kernel.
 * Every one of them is posted as an event to a userspace imaging server
 * (on Android: mm-qcamera-daemon), and the kernel BLOCKS until that server
 * acknowledges it:
 *
 *   drivers/media/platform/msm/camera_v2/camera/camera.c
 *     camera_v4l2_querycap():
 *       camera_pack_event(filep, MSM_CAMERA_GET_PARM,
 *                         MSM_CAMERA_PRIV_QUERY_CAP, -1, &event);
 *       rc = msm_post_event(&event, MSM_POST_EVT_TIMEOUT);
 *
 *   drivers/media/platform/msm/camera_v2/msm.c
 *     msm_post_event():
 *       /* send to imaging server and wait for ACK * /
 *       if (!atomic_read(&serv_running)) {
 *               pr_info("%s: daemon hasn't subscribed yet!\n", __func__);
 *               return -EIO;
 *       }
 *
 * and msm_subscribe_event() sets serv_running simply by being called:
 *
 *       rc = v4l2_event_subscribe(fh, sub, 5);
 *       if (rc == 0) atomic_set(&serv_running, 1);
 *
 * So the protocol is: open the config node, subscribe to the private event
 * type, then for every event the kernel posts, do the work and ACK it with
 * MSM_CAM_V4L2_IOCTL_CMD_ACK carrying a v4l2_event whose u.data[] holds
 * struct msm_v4l2_event_data.
 *
 * This prototype does the minimum needed to prove the protocol works: it
 * subscribes, prints every event it receives, and ACKs it with a success
 * status. That is enough to show the kernel accepts us as the imaging
 * server, which is the gate for everything else.
 *
 * Build:
 *   arm-linux-gnueabihf-gcc -O2 -static -o imaging imaging.c
 * Run (root; mm-qcamera-daemon normally holds /dev/video0):
 *   adb push imaging /data/local/tmp/ && adb shell su -c /data/local/tmp/imaging
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/videodev2.h>

/* ---- from the kernel tree: include/media/msmb_camera.h and ms m_camera.h ---- */

#define MSM_CAMERA_V4L2_EVENT_TYPE (V4L2_EVENT_PRIVATE_START + 0x00002000)

/* event ids */
#define MSM_CAMERA_NEW_SESSION	(0 + 1)
#define MSM_CAMERA_DEL_SESSION	(0 + 2)
#define MSM_CAMERA_SET_PARM	(0 + 3)
#define MSM_CAMERA_GET_PARM	(0 + 4)
#define MSM_CAMERA_MAPPING_CFG	(0 + 5)
#define MSM_CAMERA_MAPPING_SES	(0 + 6)
#define MSM_CAMERA_MSM_NOTIFY	(0 + 7)

/* commands */
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

#ifndef BASE_VIDIOC_PRIVATE
#define BASE_VIDIOC_PRIVATE 192
#endif

#define MSM_CAM_V4L2_IOCTL_CMD_ACK \
	_IOW('V', BASE_VIDIOC_PRIVATE + 32, struct v4l2_event)

struct msm_v4l2_event_data {
	unsigned int command;
	unsigned int status;
	unsigned int session_id;
	unsigned int stream_id;
	unsigned int map_op;
	unsigned int map_buf_idx;
	unsigned int notify;
	unsigned int arg_value;
	unsigned int ret_value;
	unsigned int nop3, nop4, nop5, nop6, nop7, nop8, nop9;
};

static const char *evt_name(unsigned int t)
{
	switch (t) {
	case MSM_CAMERA_NEW_SESSION: return "NEW_SESSION";
	case MSM_CAMERA_DEL_SESSION: return "DEL_SESSION";
	case MSM_CAMERA_SET_PARM:   return "SET_PARM";
	case MSM_CAMERA_GET_PARM:   return "GET_PARM";
	case MSM_CAMERA_MAPPING_CFG:return "MAPPING_CFG";
	case MSM_CAMERA_MAPPING_SES:return "MAPPING_SES";
	case MSM_CAMERA_MSM_NOTIFY: return "MSM_NOTIFY";
	default:                    return "?";
	}
}

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

int main(int argc, char **argv)
{
	int seconds = (argc > 1) ? atoi(argv[1]) : 10;
	const char *node = (argc > 2) ? argv[2] : "/dev/video0";

	int fd = open(node, O_RDWR);
	if (fd < 0) {
		printf("open(%s) failed: %s\n", node, strerror(errno));
		printf("(on Android, mm-qcamera-daemon normally holds this node)\n");
		return 1;
	}
	printf("opened %s\n", node);

	struct v4l2_event_subscription sub;
	memset(&sub, 0, sizeof(sub));
	sub.type = MSM_CAMERA_V4L2_EVENT_TYPE;
	sub.flags = 0;
	sub.id = 0;
	if (ioctl(fd, VIDIOC_SUBSCRIBE_EVENT, &sub) < 0) {
		printf("SUBSCRIBE_EVENT failed: %s\n", strerror(errno));
		close(fd);
		return 1;
	}
	printf("subscribed to event type 0x%x -- we are now the imaging server\n",
	       MSM_CAMERA_V4L2_EVENT_TYPE);

	int n = 0;
	for (int i = 0; i < seconds * 10; i++) {
		struct pollfd pfd = { .fd = fd, .events = POLLIN };
		int pr = poll(&pfd, 1, 100);
		if (pr <= 0)
			continue;

		struct v4l2_event ev;
		memset(&ev, 0, sizeof(ev));
		if (ioctl(fd, VIDIOC_DQEVENT, &ev) < 0) {
			printf("DQEVENT failed: %s\n", strerror(errno));
			break;
		}
		struct msm_v4l2_event_data *d =
			(struct msm_v4l2_event_data *)&ev.u.data[0];
		printf("event type=0x%x %-12s command=0x%x %-16s status=0x%x "
		       "session=%u stream=%u arg=%u ret=%u\n",
		       ev.type, evt_name(ev.type), d->command, cmd_name(d->command),
		       d->status, d->session_id, d->stream_id, d->arg_value,
		       d->ret_value);
		n++;

		/* ACK: success. The kernel only checks that status is not in the
		 * error range (MSM_CAMERA_ERR_EVT_BASE). */
		d->status = MSM_CAMERA_CMD_SUCESS;
		if (ioctl(fd, MSM_CAM_V4L2_IOCTL_CMD_ACK, &ev) < 0)
			printf("  ACK failed: %s\n", strerror(errno));
		else
			printf("  ACKed\n");
	}
	printf("received %d events\n", n);
	close(fd);
	return 0;
}
