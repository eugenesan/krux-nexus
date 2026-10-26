/*
 * dqprobe.c -- find the DQEVENT ioctl encoding this kernel accepts.
 *
 * The ioctl number encodes the size of struct v4l2_event, and that size
 * differs between kernels (128 in the ROM's, 120 in ours).  A wrong size
 * gives EINVAL or ENOTTY; the right one gives ENOENT ("no pending events")
 * because nothing has been posted yet.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(void)
{
	int fd = open("/dev/video0", O_RDWR | O_NONBLOCK);
	char buf[256];
	int rc;

	if (fd < 0) {
		printf("open /dev/video0: %s\n", strerror(errno));
		return 1;
	}
	for (int size = 80; size <= 140; size += 4) {
		memset(buf, 0, sizeof(buf));
		rc = ioctl(fd, _IOR('V', 89, char[size]), buf);
		printf("  size=%3d  rc=%2d  errno=%s\n", size, rc,
		       rc == 0 ? "-" : strerror(errno));
		fflush(stdout);
	}
	return 0;
}
