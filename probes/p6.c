/* Does the wait loop actually SPIN when the mount is absent?
 * Check a path that definitely has no 'media' child: /dev/graphics. */
#define SYS_write 4
typedef unsigned int u32;
static inline long sc6(long n,long a,long b,long c,long d,long e,long f){
    register long r7 __asm__("r7")=n; register long r0 __asm__("r0")=a;
    register long r1 __asm__("r1")=b; register long r2 __asm__("r2")=c;
    register long r3 __asm__("r3")=d; register long r4 __asm__("r4")=e;
    register long r5 __asm__("r5")=f;
    __asm__ volatile("svc 0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2),"r"(r3),"r"(r4),"r"(r5):"memory");
    return r0;
}
static void puts(const char*s){int n=0;while(s[n])n++;sc6(SYS_write,1,(long)s,n,0,0,0);}
static int path_exists(const char*p){static u32 st[80];
    for(unsigned i=0;i<80;i++)st[i]=0;
    return sc6(195,(long)p,(long)st,0,0,0,0)==0;}
void _start(void){
    puts("exists(/dev/graphics/media) [want 0] = ");puts(path_exists("/dev/graphics/media")?"1":"0");puts("\n");
    puts("exists(/data/media)            [want 1] = ");puts(path_exists("/data/media")?"1":"0");puts("\n");
    /* now time a 50-iteration wait on the absent path */
    unsigned long t0=0; for(int i=0;i<3000000;i++) t0++;
    puts("spin works, now looping 20x on the absent path...\n");
    int cnt=0;
    for(int i=0;i<20 && !path_exists("/dev/graphics/media");i++){ cnt++;
        for(volatile long k=0;k<200000L;k++); }
    puts("loop iterations: ");puts(cnt?"20 (correctly waited)":"0 (BUG: exited early)");puts("\n");
    for(;;)for(volatile long k=0;k<1000000L;k++);
}
