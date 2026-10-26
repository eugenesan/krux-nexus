#define O_RDONLY 0
#define SYS_write  4
#define SYS_open   5
#define SYS_close  6
#define SYS_read  45
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
    int fd=(int)sc6(SYS_open,(long)"/proc/self/mounts",O_RDONLY,0,0,0,0);
    static char buf[16384];
    long n=sc6(SYS_read,fd,(long)buf,16383,0,0,0);
    puts("first read returned: ");num(n);puts("\n");
    if(n>0){
        buf[n<16383?n:16383]=0;
        puts("--- first 120 bytes ---\n");
        sc6(SYS_write,1,(long)buf,120,0,0,0);
        puts("\n--- lines containing /data ---\n");
        const char*p=buf; const char*end=buf+n;
        while(p<end){
            const char*e=p; while(e<end&&*e!='\n')e++;
            for(const char*q=p;q+6<e;q++)
                if(q[0]==' '&&q[1]=='/'&&q[2]=='d'&&q[3]=='a'&&q[4]=='t'&&q[5]=='a'&&q[6]==' '){
                    sc6(SYS_write,1,(long)p,e-p,0,0,0);
                    sc6(SYS_write,1,(long)"\n",1,0,0,0);
                }
            p=(e<end)?e+1:end;
        }
    }
    sc6(SYS_close,fd,0,0,0,0,0);
    for(;;)for(volatile long k=0;k<1000000L;k++);
}
