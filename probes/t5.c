/* Toybox's mknod passes dev as a 64-bit value. On ARM EABI a 64-bit arg needs
 * an aligned register pair. mknod(path,mode,dev) has only 3 logical args, so
 * args go in r0,r1,r2 and the 64-bit dev needs r3-r4 -- BUT r3 is unaligned for
 * a 64-bit slot when counting from r0 as slot 0 only if we pass 4 args.
 *
 * The decisive experiment: call mknod with FOUR arguments so dev lands in the
 * (r3,r4) aligned pair, and compare against passing it in r2 only.
 */
#define SYS_mknod 33
#define SYS_write 4
#define S_IFBLK 0x6000
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
    long dev=(179L<<8)|7L;
    long r;
    /* 3-arg: dev in r2 (32-bit) */
    r=sc(SYS_mknod,(long)"/tmp/q1",S_IFBLK|0644,dev,0,0);
    puts("3-arg, dev in r2      -> ");num(r);puts("\n");
    /* 4-arg: pad r2, dev in the (r3,r4) 64-bit pair */
    r=sc(SYS_mknod,(long)"/tmp/q2",S_IFBLK|0644,0,dev,0);
    puts("4-arg, dev in (r3,r4) -> ");num(r);puts("\n");
    /* same but dev reversed */
    r=sc(SYS_mknod,(long)"/tmp/q3",S_IFBLK|0644,dev,0,0);
    puts("4-arg, dev in r3 only -> ");num(r);puts("\n");
    for(;;)for(volatile long k=0;k<1000000L;k++);
}
