/* Isolate the SECOND condition: open("/data/media", O_RDONLY|O_DIRECTORY). */
#define SYS_write  4
#define SYS_open   5
#define SYS_close  6
#define O_RDONLY 0
static inline long sc(long n,long a,long b,long c,long d,long e){
    register long r7 __asm__("r7")=n; register long r0 __asm__("r0")=a;
    register long r1 __asm__("r1")=b; register long r2 __asm__("r2")=c;
    register long r3 __asm__("r3")=d; register long r4 __asm__("r4")=e;
    __asm__ volatile("svc 0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2),"r"(r3),"r"(r4):"memory");
    return r0;
}
static void puts(const char*s){int n=0;while(s[n])n++;sc(SYS_write, 1, (long)s, n, 0, 0);}
static void num(long v){char b[24];int n=0;int neg=0;if(v==0){puts("0");return;}
    if(v<0){neg=1;v=-v;}for(;v&&n<23;v/=10)b[n++]='0'+v%10;
    if(neg)sc(SYS_write, 1, (long)"-", 1, 0, 0);while(n)sc(SYS_write, 1, (long)&b[--n], 1, 0, 0);}
static void try_open(const char*p,long flags,const char*label){
    long fd=sc(SYS_open, (long)p, flags, 0, 0, 0);
    sc(SYS_write, 1, (long)label, 0, 0, 0);
    sc(SYS_write, 1, (long)p, 0, 0, 0);
    puts(" flags=");num(flags);
    puts(" fd=");num(fd);
    puts(fd>=0?" OK\n":(fd==-22?" EINVAL\n":(fd==-2?" ENOENT\n":" err\n")));
    if(fd>=0)sc(SYS_close, fd, 0, 0, 0, 0);
}
void _start(void){
    try_open("/data/media",O_RDONLY,"O_RDONLY      ");
    try_open("/data/media",O_RDONLY|0x10000,"|O_DIRECTORY  ");
    try_open("/data/media",O_RDONLY|0x1000,"|0x1000        ");
    try_open("/data",O_RDONLY,"dir /data     ");
    try_open("/data",O_RDONLY|0x10000,"dir /data|DIR ");
    for(;;)for(volatile long k=0;k<1000000L;k++);
}
