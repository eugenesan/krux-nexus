/*
 * dqsize.c -- find the VIDIOC_DQEVENT ioctl number the running kernel wants.
 *
 * The DQEVENT ioctl number is _IOR('V', 89, struct v4l2_event), and _IOR
 * encodes sizeof(struct v4l2_event) in the number. The device is running a
 * kernel built from a DIFFERENT tree than ours (the installed KernelSU
 * kernel, 3.4.113-ge4e483b4ef6), so its struct v4l2_event may be a
 * different size -- and then our DQEVENT lands on an unknown ioctl.
 *
 * So: try every plausible size, and see which one the kernel accepts.
 * A correct size gives 0 (event dequeued) or -EAGAIN (queue empty), never
 * -EINVAL/-ENOTTY.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/videodev2.h>

#define VID_NR 89

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);

	/* O_NONBLOCK: DQEVENT with a correct ioctl number waits for an event
	 * rather than failing, so without this the probe hangs on the right
	 * answer. */
	int fd = open("/dev/video0", O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		printf("open /dev/video0: %s\n", strerror(errno));
		return 1;
	}

	/* Prove the ioctl path works at all on this node. */
	struct v4l2_capability cap;
	memset(&cap, 0, sizeof(cap));
	if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0)
		printf("QUERYCAP: %s\n", strerror(errno));
	else
		printf("QUERYCAP ok: driver=%s card=%s bus=%s version=0x%06x "
		       "caps=0x%x\n", cap.driver, cap.card, cap.bus_info,
		       cap.version, cap.capabilities);

	/* Subscribe to every event id, exactly as before. */
	for (int id = 1; id <= 7; id++) {
		struct v4l2_event_subscription sub;
		memset(&sub, 0, sizeof(sub));
		sub.type = V4L2_EVENT_PRIVATE_START + 0x2000;
		sub.id = id;
		if (ioctl(fd, VIDIOC_SUBSCRIBE_EVENT, &sub) < 0)
			printf("SUBSCRIBE id=%d: %s\n", id, strerror(errno));
	}
	printf("subscribed\n");

	printf("our struct v4l2_event is %zu bytes; our ioctl is 0x%lx\n",
	       sizeof(struct v4l2_event), (unsigned long)VIDIOC_DQEVENT);

	printf("\nprobing DQEVENT numbers by encoded size:\n");
	for (unsigned sz = 64; sz <= 256; sz += 4) {
		unsigned long cmd = (unsigned long)_IOR('V', VID_NR, char[sz]);
		unsigned char buf[256];
		memset(buf, 0, sizeof(buf));
		errno = 0;
		long r = ioctl(fd, cmd, buf);
		if (r == 0) {
			printf("  size=%3u  cmd=0x%08lx  OK (dequeued)\n", sz, cmd);
		} else if (errno != EINVAL && errno != ENOTTY) {
			printf("  size=%3u  cmd=0x%08lx  rc=%ld errno=%d (%s)\n",
			       sz, cmd, r, errno, strerror(errno));
		}
	}
	printf("(only sizes that are NOT EINVAL/ENOTTY are listed)\n");
	close(fd);
	return 0;
}
