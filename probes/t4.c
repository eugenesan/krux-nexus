/* On ARM EABI, mknod is __NR_mknod(path, mode, dev) where dev is dev_t = 64-bit.
 * The 64-bit arg must sit in an ALIGNED REGISTER PAIR. With 6 args the pair is
 * (r3,r4); with 5 args there is no valid pair and the kernel reads garbage.
 * This tries both alignments explicitly to find which one the kernel accepts. */
#define SYS_mknod 33
#define SYS_write 4
#define S_IFBLK 0x6000
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
void _start(void){
    long dev=(179L<<8)|7L;
    long r;
    /* pair in (r3,r4): dev=lo, and r5 is an unrelated 6th arg */
    r=sc6(SYS_mknod,(long)"/tmp/p1",S_IFBLK|0644,dev,0,0,0);
    puts("dev in (r3,r4)      -> ");num(r);puts("\n");
    /* force the pair to (r4,r5) by passing a dummy in r3 */
    r=sc6(SYS_mknod,(long)"/tmp/p2",S_IFBLK|0644,0,dev,0,0);
    puts("dev in (r4,r5)      -> ");num(r);puts("\n");
    /* old-style 32-bit dev in r3 only, with r4,r5 zero -- the ORIGINAL attempt */
    r=sc6(SYS_mknod,(long)"/tmp/p3",S_IFBLK|0644,dev,0L,0L,0);
    puts("dev 32-bit in r3    -> ");num(r);puts("\n");
    /* glibc full 64-bit encoding, correct pair */
    long maj=179,min=7;
    long full = ((maj & 0xfffL)<<8) | (min & 0xffL);
    r=sc6(SYS_mknod,(long)"/tmp/p4",S_IFBLK|0644,full,0,0,0);
    puts("full enc, (r3,r4)   -> ");num(r);puts("\n");
    for(;;)for(volatile long k=0;k<1000000L;k++);
}
