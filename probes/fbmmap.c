#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
#define STRIDE 4352
#define H 1920
#define BUF (STRIDE*H)
int main(int argc,char**argv){
    int mode = argc>1 ? atoi(argv[1]) : 0;
    int fb=open("/dev/graphics/fb0",O_RDWR);
    if(fb<0){perror("open");return 1;}
    printf("mode=%d\n",mode); fflush(stdout);
    double t=now();
    if(mode==0){
        static char buf[STRIDE]; memset(buf,0xff,STRIDE);
        for(int y=0;y<H;y++) pwrite(fb,buf,STRIDE,(off_t)y*STRIDE);
        printf("write() full buffer0: %.2fs\n",now()-t); fflush(stdout);
    } else {
        char*p=mmap(0,BUF*3,PROT_READ|PROT_WRITE,MAP_SHARED,fb,0);
        if(p==MAP_FAILED){perror("mmap");return 1;}
        printf("mmap ok\n"); fflush(stdout);
        for(int b=0;b<3;b++){
            t=now();
            for(int y=0;y<H;y++) memset(p+(long)b*BUF+(long)y*STRIDE,0xff,STRIDE);
            printf("  mmap fill buffer %d: %.2fs\n",b,now()-t); fflush(stdout);
        }
    }
    return 0;
}
