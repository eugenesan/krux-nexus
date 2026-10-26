#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

#define STRIDE 4352
#define H 1920
#define BUF (STRIDE * H)

/* Which of the three virtual buffers blocks on a full write? */
int main(void) {
    int fb = open("/dev/graphics/fb0", O_RDWR);
    if (fb < 0) { perror("open"); return 1; }

    static char buf[STRIDE];
    memset(buf, 0xff, STRIDE);

    for (int b = 0; b < 3; b++) {
        printf("filling buffer %d (offset %ld)... ", b, (long)b * BUF);
        fflush(stdout);
        double t = now();
        int bad = 0;
        for (int y = 0; y < H; y++) {
            ssize_t r = pwrite(fb, buf, STRIDE,
                               (off_t)((long)b * BUF + (long)y * STRIDE));
            if (r < 0) { printf("WRITE ERROR after %d rows: %s\n", y, strerror(errno));
                         bad = 1; break; }
        }
        if (!bad) printf("%.2fs\n", now() - t);
        fflush(stdout);
    }
    printf("all buffers done\n");
    return 0;
}
