#define SYS_write 4
static inline long sc(long n,long a,long b,long c,long d,long e){
    register long r7 __asm__("r7")=n; register long r0 __asm__("r0")=a;
    register long r1 __asm__("r1")=b; register long r2 __asm__("r2")=c;
    register long r3 __asm__("r3")=d; register long r4 __asm__("r4")=e;
    __asm__ volatile("svc 0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2),"r"(r3),"r"(r4):"memory");
    return r0;}
static void puts(const char*s){int n=0;while(s[n])n++;sc(4,1,(long)s,n,0,0);}
static void num(long v){char b[24];int n=0;if(v==0){puts("0");return;}
    for(;v&&n<23;v/=10)b[n++]='0'+v%10;while(n)sc(4,1,(long)&b[--n],1,0,0);}
void _start(void){
    for(int i=0;i<900;i++) for(volatile long k=0;k<200000L;k++);
    puts("900 iterations of the inner spin took: ");
    /* no clock; just report that it completed */
    puts("(see wall time above)\n");
    sc(201,0,0,0,0,0); /* exit */
    for(;;)for(volatile long k=0;k<1000000L;k++);
}
