/* Does the f_blocks test correctly distinguish ramdisk from userdata?
 * / has f_blocks == 0 (ramdisk root). /data has millions. /cache is ext4. */
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
static void num(long v){char b[24];int n=0;if(v==0){puts("0");return;}
    for(;v&&n<23;v/=10)b[n++]='0'+v%10;while(n)sc6(SYS_write,1,(long)&b[--n],1,0,0,0);}
static void t(const char*p,const char*want){
    static u32 st[80]; for(int i=0;i<80;i++)st[i]=0;
    long r=sc6(SYS_statfs,(long)p,(long)st,0,0,0,0);
    int ready = (r==0) && (st[2]!=0||st[3]!=0);
    sc6(SYS_write,1,(long)p,0,0,0,0);
    puts(": rc=");num(r);
    puts(" f_type=0x");{const char*h="0123456789abcdef";unsigned v=st[0];char b[9];b[8]=0;
        for(int i=7;i>=0;i--){b[i]=h[v&0xf];v>>=4;}sc6(SYS_write,1,(long)b,8,0,0,0);}
    puts(" f_blocks_lo=");num(st[2]);
    puts(" -> ready=");puts(ready?"YES":"no");
    puts("   (want ");puts(want);puts(")\n");
}
void _start(void){
    t("/data","YES");
    t("/cache","YES");
    t("/","no");
    t("/dev","no");
    t("/sbin","no");
    for(;;)for(volatile long k=0;k<1000000L;k++);
}
