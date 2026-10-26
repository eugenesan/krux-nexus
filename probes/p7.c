/* Which stat syscall number actually works on 32-bit ARM here?
 * 106 = __NR_stat (32-bit struct), 195 = __NR_stat64.
 * Probe a path that MUST NOT exist and one that MUST, under both numbers. */
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
static void num(long v){char b[24];int n=0;int neg=0;if(v==0){puts("0");return;}
    if(v<0){neg=1;v=-v;}for(;v&&n<23;v/=10)b[n++]='0'+v%10;
    if(neg)sc6(SYS_write,1,(long)"-",1,0,0,0);while(n)sc6(SYS_write,1,(long)&b[--n],1,0,0,0);}
static void t(int nr,const char*path){
    static u32 st[80]; for(int i=0;i<80;i++)st[i]=0;
    long r=sc6(nr,(long)path,(long)st,0,0,0,0);
    sc6(SYS_write,1,(long)path,0,0,0,0);
    puts(" nr=");num(nr);puts(" rc=");num(r);
    puts(r==0?" EXISTS\n":(r==-2?" ENOENT\n":" other\n"));
}
void _start(void){
    puts("--- EXISTING path /data/media ---\n");
    t(106,"/data/media"); t(195,"/data/media");
    puts("--- NONEXISTENT path /data/mediaZZZ ---\n");
    t(106,"/data/mediaZZZ"); t(195,"/data/mediaZZZ");
    for(;;)for(volatile long k=0;k<1000000L;k++);
}
