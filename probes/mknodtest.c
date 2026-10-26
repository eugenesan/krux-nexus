/* Isolate makedev(): create the same node three ways and see which succeeds. */
#define SYS_mknod 33
#define S_IFBLK 0x6000
static inline long sc6(long n,long a,long b,long c,long d,long e,long f){
    register long r7 __asm__("r7")=n; register long r0 __asm__("r0")=a;
    register long r1 __asm__("r1")=b; register long r2 __asm__("r2")=c;
    register long r3 __asm__("r3")=d; register long r4 __asm__("r4")=e;
    register long r5 __asm__("r5")=f;
    __asm__ volatile("svc 0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2),"r"(r3),"r"(r4),"r"(r5):"memory");
    return r0;
}
static void puts(const char*s){int n=0;while(s[n])n++;sc6(4,1,(long)s,n,0,0,0);}
void _start(void){
    /* Variant A: full glibc encoding, 32-bit low word only */
    long devA = (179L << 8) | 7L;
    long rA = sc6(SYS_mknod,(long)"/tmp/nA",S_IFBLK|0644,devA,0,0,0);
    puts("A (maj<<8|min)      -> "); puts(rA==0?"OK":"FAIL"); puts("\n");

    /* Variant B: same value but passed as a genuine 64-bit pair, lo/hi split */
    long lo = (179L<<8)|7L, hi = 0;
    long rB = sc6(SYS_mknod,(long)"/tmp/nB",S_IFBLK|0644,lo,hi,0,0);
    puts("B (lo,hi as pair)   -> "); puts(rB==0?"OK":"FAIL"); puts("\n");

    /* Variant C: the glibc makedev with the high word explicitly 0 --
       this is what init-dual actually did via makedev() */
    long maj=179, min=7;
    long devC = (((maj & 0xfffff00LL)<<32) | ((maj & 0xfffLL)<<8) |
                 ((min & 0xffffff00LL)<<12) | (min & 0xffLL)) & 0xffffffffL;
    long rC = sc6(SYS_mknod,(long)"/tmp/nC",S_IFBLK|0644,devC,0,0,0);
    puts("C (glibc formula)   -> "); puts(rC==0?"OK":"FAIL"); puts("\n");

    /* Variant D: no mode type bits at all */
    long rD = sc6(SYS_mknod,(long)"/tmp/nD",0644,devA,0,0,0);
    puts("D (mode 0644 only)  -> "); puts(rD==0?"OK":"FAIL"); puts("\n");
    for(;;) for(volatile long k=0;k<1000000L;k++);
}
