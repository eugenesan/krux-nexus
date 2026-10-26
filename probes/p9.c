#define SYS_write  4
#define SYS_statfs 99
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
static void hex(unsigned v){const char*h="0123456789abcdef";char b[9];b[8]=0;
    for(int i=7;i>=0;i--){b[i]=h[v&0xf];v>>=4;}sc6(SYS_write,1,(long)b,8,0,0,0);}
static void t(const char*p){
    static u32 st[80]; for(int i=0;i<80;i++)st[i]=0;
    long r=sc6(SYS_statfs,(long)p,(long)st,0,0,0,0);
    sc6(SYS_write,1,(long)p,0,0,0,0);
    puts(" rc=");{char b[24];int n=0;long v=r;if(v==0){puts("0");}else{ if(v<0){sc6(SYS_write,1,(long)"-",1,0,0,0);v=-v;}
        for(;v&&n<23;v/=10)b[n++]='0'+v%10;while(n)sc6(SYS_write,1,(long)&b[--n],1,0,0,0);}}
    puts(" type=0x");hex(st[0]);
    puts(" blocks_lo=0x");hex(st[2]);
    puts(" blocks_hi=0x");hex(st[3]);
    puts(" ready=");puts((r==0&&(st[2]||st[3]))?"YES":"no");puts("\n");
}
void _start(void){
    puts("ramdisk-only dirs (must all be 'no'):\n");
    t("/sbin"); t("/res"); t("/twres"); t("/etc");
    puts("real mounts (may be yes):\n");
    t("/data"); t("/cache"); t("/system");
    for(;;)for(volatile long k=0;k<1000000L;k++);
}
