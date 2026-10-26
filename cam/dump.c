/*
 * dump.c -- dequeue one camera event and hexdump it raw.
 *
 * The running device's kernel wants a 128-byte struct v4l2_event (its
 * DQEVENT ioctl number is 0x80805659), while our tree's header says 136
 * (0x80885659). Rather than guess the field offsets, take the raw bytes and
 * look at them.
 *
 * The trigger is a child process that opens the rear sensor node: the
 * kernel's open() posts a NEW_SESSION event and blocks until it is
 * acknowledged, which is what gives us something to dequeue.
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
#define DQ_IOCTL   _IOR('V', 89, char[EVSZ])
#define ACK_IOCTL  _IOW('V', 224, char[EVSZ])

#define EVTYPE (V4L2_EVENT_PRIVATE_START + 0x2000)

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	const char *node = (argc > 1) ? argv[1] : "/dev/video1";

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
	printf("subscribed to ids 1..7 (type 0x%x)\n", EVTYPE);

	pid_t pid = fork();
	if (pid == 0) {
		int cfd = open(node, O_RDWR);
		if (cfd < 0) {
			printf("[child] open(%s): %s\n", node, strerror(errno));
			_exit(1);
		}
		printf("[child] opened %s\n", node);
		_exit(0);
	}

	for (int i = 0; i < 300; i++) {
		struct pollfd pfd = { .fd = fd, .events = POLLIN };
		if (poll(&pfd, 1, 100) <= 0) {
			int st;
			if (waitpid(pid, &st, WNOHANG) == pid) {
				printf("child exited, no event\n");
				break;
			}
			continue;
		}
		unsigned char buf[EVSZ];
		memset(buf, 0, sizeof(buf));
		long r = ioctl(fd, DQ_IOCTL, buf);
		if (r < 0) {
			printf("DQEVENT: %s\n", strerror(errno));
			break;
		}
		printf("GOT EVENT, %d bytes raw:\n", EVSZ);
		for (int o = 0; o < EVSZ; o += 16) {
			printf("  %03x: ", o);
			for (int b = 0; b < 16; b++)
				printf("%02x ", buf[o + b]);
			printf(" |");
			for (int b = 0; b < 16; b += 4) {
				unsigned v = buf[o + b] | (buf[o + b + 1] << 8) |
					     (buf[o + b + 2] << 16) |
					     (buf[o + b + 3] << 24);
				printf(" %10u", v);
			}
			printf(" |\n");
		}
		break;
	}
	return 0;
}
