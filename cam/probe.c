/*
 * probe.c -- explore the Nexus 5 camera from userspace.
 *
 * The point of this program is to find out, empirically and on a device whose
 * camera is known to work, exactly which V4L2 nodes exist, what the
 * capabilities and formats are, and whether a *plain* V4L2 multi-planar
 * capture works without the Qualcomm camera HAL.
 *
 * That last question is the whole design: if VIDIOC_REQBUFS / S_FMT /
 * QBUF / STREAMON / DQBUF works on the "msm-sensor" node, we need no vendor
 * blob at all. If it does not, we have to reimplement the CAM_CAMERA_V4L2
 * ioctl protocol by hand.
 *
 * ABI NOTE. This uses the CAF 3.4 layout, which is what our kernel tree
 * carries and what /usr/include/linux/videodev2.h on this host happens to
 * contain:
 *   - there is no separate VIDIOC_S_FMT_VID_CAP_MPLANE. Multi-planar is
 *     selected by fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE on the
 *     ordinary VIDIOC_S_FMT.
 *   - struct v4l2_buffer.m.planes is a POINTER to an array of
 *     struct v4l2_plane, and there is one scalar length, not length[].
 * That is the 2.6.32-era mplane design. Upstream later changed it to
 * length[]/m.planes[]. The probe prints sizeof() of the relevant structs and
 * dumps the raw buffer bytes after QUERYBUF, so that if the running kernel
 * turns out to use the newer layout we can see exactly how.
 *
 * Build:
 *   arm-linux-gnueabihf-gcc -O2 -static -o probe probe.c
 * Run (needs root; the video nodes are root:system 0660):
 *   adb push probe /data/local/tmp/ && adb shell su -c /data/local/tmp/probe
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <linux/videodev2.h>

#define MAX_PLANES 8

static const char *fourcc_str(unsigned int f)
{
	static char b[5];
	b[0] = (f >> 0) & 0xff;
	b[1] = (f >> 8) & 0xff;
	b[2] = (f >> 16) & 0xff;
	b[3] = (f >> 24) & 0xff;
	b[4] = 0;
	for (int i = 0; i < 4; i++)
		if (b[i] < 32 || b[i] > 126)
			b[i] = '.';
	return b;
}

static void caps_str(unsigned int c, char *out, size_t n)
{
	static const struct { unsigned int bit; const char *name; } t[] = {
		{ V4L2_CAP_VIDEO_CAPTURE,   "VIDEO_CAPTURE" },
		{ V4L2_CAP_VIDEO_OUTPUT,    "VIDEO_OUTPUT" },
		{ V4L2_CAP_VIDEO_M2M,       "VIDEO_M2M" },
		{ V4L2_CAP_STREAMING,       "STREAMING" },
		{ V4L2_CAP_READWRITE,       "READWRITE" },
		{ V4L2_CAP_DEVICE_CAPS,     "DEVICE_CAPS" },
		{ V4L2_CAP_EXT_PIX_FORMAT,  "EXT_PIX_FORMAT" },
	};
	out[0] = 0;
	for (unsigned i = 0; i < sizeof(t) / sizeof(t[0]); i++)
		if (c & t[i].bit) {
			if (out[0])
				strncat(out, "|", n - strlen(out) - 1);
			strncat(out, t[i].name, n - strlen(out) - 1);
		}
}

static const char *nodes[] = {
	"/dev/video0", "/dev/video1", "/dev/video2",
	"/dev/video3", "/dev/video4", "/dev/video5",
};
#define NNODES ((int)(sizeof(nodes) / sizeof(nodes[0])))

static void probe_querycap(const char *path)
{
	struct v4l2_capability cap;
	int fd = open(path, O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		printf("  %-14s open failed: %s\n", path, strerror(errno));
		return;
	}
	memset(&cap, 0, sizeof(cap));
	if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
		printf("  %-14s QUERYCAP failed: %s\n", path, strerror(errno));
		close(fd);
		return;
	}
	char c[256];
	caps_str(cap.capabilities, c, sizeof(c));
	printf("  %-14s driver=%-14s card=%-22s caps=%s\n",
	       path, cap.driver, cap.card, c);
	if (cap.device_caps != cap.capabilities) {
		caps_str(cap.device_caps, c, sizeof(c));
		printf("  %-14s   device_caps=%s\n", "", c);
	}
	close(fd);
}

static void probe_formats(const char *path)
{
	int fd = open(path, O_RDWR | O_NONBLOCK);
	if (fd < 0)
		return;
	struct v4l2_fmtdesc fd_;
	memset(&fd_, 0, sizeof(fd_));
	fd_.index = 0;
	int n = 0;
	while (ioctl(fd, VIDIOC_ENUM_FMT, &fd_) == 0 && n < 16) {
		printf("  %-14s fmt[%u] %-5s %-28s %s\n",
		       path, fd_.index, fourcc_str(fd_.pixelformat),
		       fd_.description,
		       (fd_.flags & V4L2_FMT_FLAG_COMPRESSED) ? "compressed" : "");
		fd_.index++;
		n++;
	}
	close(fd);
}

static void probe_sizes(const char *path)
{
	int fd = open(path, O_RDWR | O_NONBLOCK);
	if (fd < 0)
		return;
	struct v4l2_fmtdesc fd_;
	memset(&fd_, 0, sizeof(fd_));
	fd_.index = 0;
	int n = 0;
	while (ioctl(fd, VIDIOC_ENUM_FMT, &fd_) == 0 && n < 4) {
		struct v4l2_frmsizeenum fs;
		memset(&fs, 0, sizeof(fs));
		fs.index = 0;
		fs.pixel_format = fd_.pixelformat;
		int m = 0;
		while (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &fs) == 0 && m < 12) {
			printf("  %-14s   %s %ux%s\n", path, fs.type == V4L2_FRMSIZE_TYPE_DISCRETE ? "disc" : "cont",
			       fs.discrete.width, fs.type == V4L2_FRMSIZE_TYPE_DISCRETE ? "" : "x?");
			fs.index++;
			m++;
		}
		fd_.index++;
		n++;
	}
	close(fd);
}

static void probe_controls(const char *path)
{
	int fd = open(path, O_RDWR | O_NONBLOCK);
	if (fd < 0)
		return;
	struct v4l2_queryctrl qc;
	memset(&qc, 0, sizeof(qc));
	qc.id = V4L2_CID_BASE;
	int shown = 0;
	while (ioctl(fd, VIDIOC_QUERYCTRL, &qc) == 0) {
		if (qc.id == V4L2_CID_USER_BASE ||
		    qc.id == V4L2_CID_POWER_LINE_FREQUENCY) {
			qc.id++;
			continue;
		}
		printf("  %-14s ctrl 0x%04x %-24s min=%-6d max=%-6d\n",
		       path, qc.id, qc.name, qc.minimum, qc.maximum);
		qc.id++;
		if (++shown > 20)
			break;
	}
	close(fd);
}

static int try_capture_mplane(const char *path, int width, int height,
			      int pixfmt, const char *outbase)
{
	int fd = open(path, O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		printf("  %-14s capture: open failed: %s\n", path, strerror(errno));
		return -1;
	}

	struct v4l2_capability cap;
	memset(&cap, 0, sizeof(cap));
	if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
		printf("  %-14s capture: QUERYCAP failed: %s\n", path, strerror(errno));
		close(fd);
		return -1;
	}
	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		printf("  %-14s capture: not a capture device\n", path);
		close(fd);
		return -1;
	}

	struct v4l2_format fmt;
	memset(&fmt, 0, sizeof(fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	fmt.fmt.pix_mp.width = width;
	fmt.fmt.pix_mp.height = height;
	fmt.fmt.pix_mp.pixelformat = pixfmt;
	fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;
	fmt.fmt.pix_mp.num_planes = 3;
	if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
		printf("  %-14s capture: S_FMT(mplane) failed: %s\n",
		       path, strerror(errno));
		close(fd);
		return -1;
	}
	printf("  %-14s capture: negotiated %ux%u fmt=%s planes=%u\n", path,
	       fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height,
	       fourcc_str(fmt.fmt.pix_mp.pixelformat),
	       fmt.fmt.pix_mp.num_planes);

	struct v4l2_requestbuffers rb;
	memset(&rb, 0, sizeof(rb));
	rb.count = 4;
	rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	rb.memory = V4L2_MEMORY_MMAP;
	if (ioctl(fd, VIDIOC_REQBUFS, &rb) < 0) {
		printf("  %-14s capture: REQBUFS failed: %s\n", path, strerror(errno));
		close(fd);
		return -1;
	}
	printf("  %-14s capture: got %u buffers\n", path, rb.count);
	if (rb.count < 2) {
		close(fd);
		return -1;
	}

	unsigned int nplanes = fmt.fmt.pix_mp.num_planes;
	if (nplanes > MAX_PLANES)
		nplanes = MAX_PLANES;

	struct v4l2_buffer *bufs = calloc(rb.count, sizeof(*bufs));
	struct v4l2_plane *planes = calloc(rb.count, sizeof(*planes) * MAX_PLANES);
	void **maps = calloc(rb.count * MAX_PLANES, sizeof(void *));
	if (!bufs || !planes || !maps) {
		free(bufs); free(planes); free(maps); close(fd);
		return -1;
	}

	for (unsigned i = 0; i < rb.count; i++) {
		struct v4l2_buffer b;
		memset(&b, 0, sizeof(b));
		b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		b.memory = V4L2_MEMORY_MMAP;
		b.index = i;
		memset(&planes[i * MAX_PLANES], 0, sizeof(struct v4l2_plane) * MAX_PLANES);
		b.m.planes = &planes[i * MAX_PLANES];
		if (ioctl(fd, VIDIOC_QUERYBUF, &b) < 0) {
			printf("  %-14s capture: QUERYBUF %u failed: %s\n",
			       path, i, strerror(errno));
			goto fail;
		}
		printf("  %-14s capture: buf%u length=%u", path, i, b.length);
		for (unsigned p = 0; p < nplanes; p++)
			printf(" p%u(len=%u off=0x%x)", p,
			       planes[i * MAX_PLANES + p].length,
			       planes[i * MAX_PLANES + p].m.mem_offset);
		printf("\n");
		for (unsigned p = 0; p < nplanes; p++) {
			unsigned len = planes[i * MAX_PLANES + p].length;
			if (!len)
				continue;
			maps[i * MAX_PLANES + p] =
				mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED,
				     fd, planes[i * MAX_PLANES + p].m.mem_offset);
			if (maps[i * MAX_PLANES + p] == MAP_FAILED) {
				printf("  %-14s capture: mmap p%u failed: %s\n",
				       path, p, strerror(errno));
				maps[i * MAX_PLANES + p] = NULL;
			}
		}
		bufs[i] = b;
		if (ioctl(fd, VIDIOC_QBUF, &bufs[i]) < 0) {
			printf("  %-14s capture: QBUF %u failed: %s\n",
			       path, i, strerror(errno));
			goto fail;
		}
	}

	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
		printf("  %-14s capture: STREAMON failed: %s\n", path, strerror(errno));
		goto fail;
	}
	printf("  %-14s capture: streaming\n", path);

	struct pollfd pfd = { .fd = fd, .events = POLLIN };
	int pr = poll(&pfd, 1, 5000);
	if (pr <= 0) {
		printf("  %-14s capture: NO FRAME within 5s (poll=%d)\n", path, pr);
		ioctl(fd, VIDIOC_STREAMOFF, &type);
		goto fail;
	}

	struct v4l2_buffer dq;
	memset(&dq, 0, sizeof(dq));
	dq.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	memset(&planes[0], 0, sizeof(struct v4l2_plane) * MAX_PLANES);
	dq.m.planes = &planes[0];
	if (ioctl(fd, VIDIOC_DQBUF, &dq) < 0) {
		printf("  %-14s capture: DQBUF failed: %s\n", path, strerror(errno));
		ioctl(fd, VIDIOC_STREAMOFF, &type);
		goto fail;
	}
	printf("  %-14s capture: GOT FRAME index=%u bytesused=%u\n",
	       path, dq.index, dq.bytesused);

	int w = fmt.fmt.pix_mp.width, h = fmt.fmt.pix_mp.height;
	int yb = fmt.fmt.pix_mp.plane_fmt[0].bytesperline;
	if (yb <= 0)
		yb = w;

	char name[256];
	snprintf(name, sizeof(name), "%s.y.pgm", outbase);
	FILE *f = fopen(name, "wb");
	if (f && maps[dq.index * MAX_PLANES]) {
		fprintf(f, "P5\n%d %d\n255\n", w, h);
		fwrite(maps[dq.index * MAX_PLANES], 1, (size_t)yb * h, f);
		fclose(f);
		printf("  %-14s capture: wrote %s (%dx%d grey)\n", path, name, w, h);
	}

	snprintf(name, sizeof(name), "%s.ppm", outbase);
	f = fopen(name, "wb");
	if (f && nplanes >= 3 && maps[dq.index * MAX_PLANES + 1] &&
	    maps[dq.index * MAX_PLANES + 2]) {
		unsigned char *y = maps[dq.index * MAX_PLANES];
		unsigned char *u = maps[dq.index * MAX_PLANES + 1];
		unsigned char *v = maps[dq.index * MAX_PLANES + 2];
		int cb = fmt.fmt.pix_mp.plane_fmt[1].bytesperline;
		if (cb <= 0)
			cb = w / 2;
		fprintf(f, "P6\n%d %d\n255\n", w, h);
		for (int yy = 0; yy < h; yy++) {
			for (int xx = 0; xx < w; xx++) {
				int Y = y[yy * yb + xx] - 16;
				int U = u[(yy / 2) * cb + xx / 2] - 128;
				int V = v[(yy / 2) * cb + xx / 2] - 128;
				int r = (298 * Y + 409 * V + 128) >> 8;
				int g = (298 * Y - 100 * U - 208 * V + 128) >> 8;
				int b = (298 * Y + 516 * U + 128) >> 8;
				if (r < 0) r = 0; if (r > 255) r = 255;
				if (g < 0) g = 0; if (g > 255) g = 255;
				if (b < 0) b = 0; if (b > 255) b = 255;
				unsigned char px[3] = { r, g, b };
				fwrite(px, 1, 3, f);
			}
		}
		fclose(f);
		printf("  %-14s capture: wrote %s (colour)\n", path, name);
	} else if (f) {
		fclose(f);
	}

	ioctl(fd, VIDIOC_STREAMOFF, &type);
	for (unsigned i = 0; i < rb.count; i++)
		for (unsigned p = 0; p < nplanes; p++)
			if (maps[i * MAX_PLANES + p])
				munmap(maps[i * MAX_PLANES + p],
				       planes[i * MAX_PLANES + p].length);
	free(bufs); free(planes); free(maps); close(fd);
	return 0;

fail:
	for (unsigned i = 0; i < rb.count; i++)
		for (unsigned p = 0; p < nplanes; p++)
			if (maps[i * MAX_PLANES + p])
				munmap(maps[i * MAX_PLANES + p],
				       planes[i * MAX_PLANES + p].length);
	free(bufs); free(planes); free(maps); close(fd);
	return -1;
}

int main(void)
{
	printf("sizeof v4l2_buffer=%zu v4l2_format=%zu v4l2_plane=%zu\n",
	       sizeof(struct v4l2_buffer), sizeof(struct v4l2_format),
	       sizeof(struct v4l2_plane));
	printf("sizeof v4l2_pix_format_mplane=%zu  VIDEO_MAX_PLANES=%d\n",
	       sizeof(struct v4l2_pix_format_mplane), VIDEO_MAX_PLANES);
	printf("VIDIOC_S_FMT=0x%lx  V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE=%d\n\n",
	       (unsigned long)VIDIOC_S_FMT, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);

	printf("=== QUERYCAP ===\n");
	for (int i = 0; i < NNODES; i++)
		probe_querycap(nodes[i]);

	printf("\n=== FORMATS ===\n");
	for (int i = 0; i < NNODES; i++)
		probe_formats(nodes[i]);

	printf("\n=== FRAME SIZES ===\n");
	for (int i = 0; i < NNODES; i++)
		probe_sizes(nodes[i]);

	printf("\n=== CONTROLS ===\n");
	for (int i = 0; i < NNODES; i++)
		probe_controls(nodes[i]);

	printf("\n=== CAPTURE: 640x480 YUV420 ===\n");
	for (int i = 0; i < NNODES; i++)
		try_capture_mplane(nodes[i], 640, 480, V4L2_PIX_FMT_YUV420,
				   "/data/local/tmp/cam");

	printf("\n=== CAPTURE: 320x240 YUV420 ===\n");
	for (int i = 0; i < NNODES; i++)
		try_capture_mplane(nodes[i], 320, 240, V4L2_PIX_FMT_YUV420,
				   "/data/local/tmp/cam320");

	return 0;
}
