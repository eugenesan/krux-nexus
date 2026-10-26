#define SYS_mknod 33
#define SYS_write 4
#define S_IFBLK 0x6000
#define S_IFCHR 0x2000
static inline long sc6(long n,long a,long b,long c,long d,long e,long f){
    register long r7 __asm__("r7")=n; register long r0 __asm__("r0")=a;
    register long r1 __asm__("r1")=b; register long r2 __asm__("r2")=c;
    register long r3 __asm__("r3")=d; register long r4 __asm__("r4")=e;
    register long r5 __asm__("r5")=f;
    __asm__ volatile("svc 0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2),"r"(r3),"r"(r4),"r"(r5):"memory");
    return r0;
}
static inline long sc(long n,long a,long b,long c,long d,long e){
    register long r7 __asm__("r7")=n; register long r0 __asm__("r0")=a;
    register long r1 __asm__("r1")=b; register long r2 __asm__("r2")=c;
    register long r3 __asm__("r3")=d; register long r4 __asm__("r4")=e;
    __asm__ volatile("svc 0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2),"r"(r3),"r"(r4):"memory");
    return r0;
}
static void puts(const char*s){int n=0;while(s[n])n++;sc(SYS_write,1,(long)s,n,0,0);}
static void num(long v){char b[24];int n=0;int neg=0;if(v==0){puts("0");return;}
    if(v<0){neg=1;v=-v;}for(;v&&n<23;v/=10)b[n++]='0'+v%10;
    if(neg)sc(SYS_write,1,(long)"-",1,0,0);while(n)sc(SYS_write,1,(long)&b[--n],1,0,0);}
void _start(void){
    long dev = (179L<<8)|7L;
    long r;
    r=sc6(SYS_mknod,(long)"/tmp/cA",S_IFBLK|0644,dev,0,0,0);
    puts("mknod block /tmp/cA -> ");num(r);puts("  errno=");num(r<0?-r:0);puts("\n");
    r=sc6(SYS_mknod,(long)"/tmp/cB",S_IFCHR|0600,dev,0,0,0);
    puts("mknod char  /tmp/cB -> ");num(r);puts("  errno=");num(r<0?-r:0);puts("\n");
    r=sc6(SYS_mknod,(long)"/tmp/cC",S_IFBLK,dev,0,0,0);
    puts("mknod blk mode=0 -> ");num(r);puts("  errno=");num(r<0?-r:0);puts("\n");
    for(;;)for(volatile long k=0;k<1000000L;k++);
}
