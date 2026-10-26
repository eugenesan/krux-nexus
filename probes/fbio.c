/* Which ioctls does this msm_fb driver actually answer?
 * Our earlier run reported smem_len=1920 line_length=16, which are NOT real
 * values (real: ~16 MB, 4352). Hypothesis: FBIOGET_FSCREENINFO (0x4600) is
 * not supported here and the ioctl returned an error we mis-read, OR it
 * returned 0 without filling the struct, so we read uninitialised memory.
 * Print the ioctl RETURN VALUE and a known-poisoned buffer. */
typedef unsigned int u32;
#define O_RDWR 2
#define SYS_write 4
#define SYS_open 5
#define SYS_close 6
#define SYS_ioctl 54
static inline long sc(long n,long a,long b,long c,long d,long e){
    register long r7 __asm__("r7")=n; register long r0 __asm__("r0")=a;
    register long r1 __asm__("r1")=b; register long r2 __asm__("r2")=c;
    register long r3 __asm__("r3")=d; register long r4 __asm__("r4")=e;
    __asm__ volatile("svc 0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2),"r"(r3),"r"(r4):"memory");
    return r0;
}
static void say(const char*s){int n=0;while(s[n])n++;sc(SYS_write, 1, (long)s, n, 0, 0);}
static void hex(u32 v){const char*h="0123456789abcdef";char b[9];b[8]=0;
    for(int i=7;i>=0;i--){b[i]=h[v&0xf];v>>=4;}sc(SYS_write, 1, (long)b, 8, 0, 0);}
static void num(long v){char b[24];int n=0;int ng=0;if(v<0){ng=1;v=-v;}
    if(v==0){sc(SYS_write, 1, (long)"0", 1, 0, 0);return;}
    for(;v&&n<23;v/=10)b[n++]='0'+v%10;
    if(ng)sc(SYS_write, 1, (long)"-", 1, 0, 0);
    while(n)sc(SYS_write, 1, (long)&b[--n], 1, 0, 0);}
int main(void){
    int fd=(int)sc(SYS_open, (long)"/dev/graphics/fb0", O_RDWR, 0, 0, 0);
    if(fd<0){say("open failed\n");return 1;}
    say("fd=");num(fd);say("\n");

    /* Poison the buffer so we can tell "not filled" from "filled". */
    static u32 buf[64];
    for(int i=0;i<64;i++) buf[i]=0xDEADBEEFu;

    long r=sc(SYS_ioctl, fd, 0x4600, (long)buf, 0, 0);
    say("FBIOGET_FSCREENINFO(0x4600) rc=");num(r);say("\n");
    say("  word0(id[0..3])=");hex(buf[0]);
    say(" w4(smem_start)=");hex(buf[4]);
    say(" w5(smem_len)=");hex(buf[5]);
    say(" w11(line_length)=");hex(buf[11]);
    say("\n");
    int poisoned=1;
    for(int i=0;i<64;i++) if(buf[i]==0xDEADBEEFu) poisoned=1;
    say(poisoned?"  (buffer still partly poisoned -> ioctl did NOT fill it)\n"
                :"  (buffer fully overwritten -> ioctl filled it)\n");

    /* Also try the variable-screeninfo ioctl, which is what mdss actually uses */
    for(int i=0;i<64;i++) buf[i]=0xDEADBEEFu;
    r=sc(SYS_ioctl, fd, 0x4601, (long)buf, 0, 0);   /* FBIOGET_VSCREENINFO */
    say("FBIOGET_VSCREENINFO(0x4601) rc=");num(r);say("\n");
    say("  w0=");hex(buf[0]);say(" w1=");hex(buf[1]);say(" w2=");hex(buf[2]);
    say(" w3=");hex(buf[3]);say("\n");
    sc(SYS_close, fd, 0, 0, 0, 0);
    return 0;
}
