#define SYS_open 5
#define SYS_close 6
#define SYS_mkdir 39
#define SYS_write 4
#define O_WRONLY 1
#define O_CREAT 0x40
static inline long sc(long n,long a,long b,long c,long d,long e){
    register long r7 __asm__("r7")=n; register long r0 __asm__("r0")=a;
    register long r1 __asm__("r1")=b; register long r2 __asm__("r2")=c;
    register long r3 __asm__("r3")=d; register long r4 __asm__("r4")=e;
    __asm__ volatile("svc 0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2),"r"(r3),"r"(r4):"memory");
    return r0;
}
static void puts(const char*s){int n=0;while(s[n])n++;sc(SYS_write,1,(long)s,n,0,0);}
static void num(long v){char b[24];int n=0;if(v==0){puts("0");return;}
    int neg=0; if(v<0){neg=1;v=-v;} for(;v&&n<23;v/=10)b[n++]='0'+v%10;
    if(neg)sc(SYS_write,1,(long)"-",1,0,0);
    while(n)sc(SYS_write,1,(long)&b[--n],1,0,0);}
void _start(void){
    long r;
    r=sc(SYS_mkdir,(long)"/tmp/d1",0755,0,0,0); puts("mkdir /tmp/d1 -> ");num(r);puts("\n");
    r=sc(SYS_open,(long)"/tmp/d1/f",O_WRONLY|O_CREAT,0644,0,0); puts("open+creat file -> ");num(r);
    if(r>=0)sc(SYS_close,r,0,0,0,0); puts("\n");
    r=sc(SYS_open,(long)"/tmp/d1/f",O_WRONLY,0,0,0); puts("reopen file   -> ");num(r);puts("\n");
    for(;;)for(volatile long k=0;k<1000000L;k++);
}
