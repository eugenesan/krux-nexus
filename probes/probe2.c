/* Where do pwrite64 and pread64 actually land?
 *
 * Symptom: writing magenta to plane 0 (offset 0) and green to plane 1
 * (offset FB_BUFSZ) returns success for every row, but reading row 960 of
 * plane 0 AND row 960 of plane 1 both return 0xff00ff00 (green).
 *
 * So EITHER both reads hit the same buffer, OR the plane-0 write went
 * somewhere else. Test each hypothesis directly:
 *   A) read plane 0 and plane 1 after writing a DISTINCT value to each
 *   B) read a few different row offsets to see what varies
 *   C) try the "second plane" at the offset the driver reports, not our guess
 */
typedef unsigned int u32;
#define O_RDWR 2
#define SYS_write 4
#define SYS_open 5
#define SYS_close 6
#define SYS_pwrite64 181
#define SYS_pread64 180
#define SYS_ioctl 54
#define FB_W 1080
#define FB_H 1920
#define FB_STRIDE 4352
#define FB_ROWU (FB_STRIDE/4)
#define FB_BUFSZ (FB_STRIDE*FB_H)
static inline long sc(long n,long a,long b,long c,long d,long e){
    register long r7 __asm__("r7")=n; register long r0 __asm__("r0")=a;
    register long r1 __asm__("r1")=b; register long r2 __asm__("r2")=c;
    register long r3 __asm__("r3")=d; register long r4 __asm__("r4")=e;
    __asm__ volatile("svc 0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2),"r"(r3),"r"(r4):"memory");
    return r0;
}
static inline long sc6(long n,long a,long b,long c,long d,long e,long f){
    register long r7 __asm__("r7")=n; register long r0 __asm__("r0")=a;
    register long r1 __asm__("r1")=b; register long r2 __asm__("r2")=c;
    register long r3 __asm__("r3")=d; register long r4 __asm__("r4")=e;
    register long r5 __asm__("r5")=f;
    __asm__ volatile("svc 0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2),"r"(r3),"r"(r4),"r"(r5):"memory");
    return r0;
}
static void say(const char*s){int n=0;while(s[n])n++;sc(SYS_write, 1, (long)s, n, 0, 0);}
static void hex(u32 v){const char*h="0123456789abcdef";char b[9];b[8]=0;
    for(int i=7;i>=0;i--){b[i]=h[v&0xf];v>>=4;}sc(SYS_write, 1, (long)b, 8, 0, 0);}
static void num(long v){char b[24];int n=0;int ng=0;if(v<0){ng=1;v=-v;}
    if(v==0){sc(SYS_write, 1, (long)"0", 1, 0, 0);return;}
    for(;v&&n<23;v/=10)b[n++]='0'+v%10;
    if(ng)sc(SYS_write, 1, (long)"-", 1, 0, 0);
    while(n)sc(SYS_write, 1, (long)&b[--n], 1, 0, 0);}
int main(void){
    int fd=(int)sc(SYS_open, (long)"/dev/graphics/fb0", O_RDWR, 0, 0, 0);
    if(fd<0){say("open failed\n");return 1;}
    say("FB_BUFSZ = ");num(FB_BUFSZ);say("\n");

    /* write a DISTINCT value per plane: plane0=0x11111111 plane1=0x22222222 */
    static u32 row[FB_ROWU];
    for(int i=0;i<FB_ROWU;i++)row[i]=0x11111111;
    for(int y=0;y<FB_H;y++) sc6(SYS_pwrite64,fd,(long)row,FB_STRIDE,(long)y*FB_STRIDE,0,0);
    for(int i=0;i<FB_ROWU;i++)row[i]=0x22222222;
    for(int y=0;y<FB_H;y++) sc6(SYS_pwrite64,fd,(long)row,FB_STRIDE,
                                (long)FB_BUFSZ+(long)y*FB_STRIDE,0,0);
    say("wrote plane0=11111111 plane1=22222222\n");

    static u32 back[FB_ROWU];
    long offs[6]={0, 960L*FB_STRIDE, 1919L*FB_STRIDE,
                  FB_BUFSZ, FB_BUFSZ+960L*FB_STRIDE, FB_BUFSZ+1919L*FB_STRIDE};
    const char*lbl[6]={"p0 row0   ","p0 row960 ","p0 row1919",
                       "p1 row0   ","p1 row960 ","p1 row1919"};
    for(int k=0;k<6;k++){
        long r=sc6(SYS_pread64,fd,(long)back,FB_STRIDE,offs[k],0,0);
        say("  ");say(lbl[k]);say(" off=");num(offs[k]);
        say(" read=");num(r);say(" val=");
        if(r==FB_STRIDE)hex(back[100]);else say("(n/a)");
        say("\n");
    }
    return 0;
}
