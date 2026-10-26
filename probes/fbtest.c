#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
int main(void){
    int fb=open("/dev/graphics/fb0",O_RDWR);
    printf("open fd=%d\n",fb); if(fb<0){perror("open");return 1;}
    static char buf[4352];
    memset(buf,0xff,sizeof buf);
    printf("single-row pwrite to buffer0... "); fflush(stdout);
    double t=now();
    ssize_t r=pwrite(fb,buf,4352,0);
    printf("r=%zd in %.3fs\n",r,now()-t);
    printf("100 rows... "); fflush(stdout); t=now();
    for(int y=0;y<100;y++) pwrite(fb,buf,4352,(off_t)y*4352);
    printf("ok in %.3fs\n",now()-t);
    printf("to buffer 1 (offset 8355840)... "); fflush(stdout); t=now();
    r=pwrite(fb,buf,4352,(off_t)8355840);
    printf("r=%zd in %.3fs\n",r,now()-t);
    printf("to buffer 2 (offset 16711680)... "); fflush(stdout); t=now();
    r=pwrite(fb,buf,4352,(off_t)16711680);
    printf("r=%zd in %.3fs\n",r,now()-t);
    return 0;
}
