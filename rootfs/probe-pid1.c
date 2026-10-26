/* probe-pid1.c -- minimise the /init to the point of no return.
 *
 * Test O (puts + 90M spin + reboot) hung for a minute, but its spin is only
 * ~0.15 s. It never reached the spin. So either the kernel never jumped to the
 * entry point, or it hung in the first instructions.
 *
 * This version does the ONE thing that has an unmistakable external effect,
 * with no puts, no stack swap, no loop, no libc, nothing:
 *
 *     -> power off within a second means the kernel DID load and enter our
 *        init, and the hang is in the entry code (stack swap / puts / spin)
 *     -> hang means the kernel never ran /init at all
 */
#define SYS_reboot 142            /* __NR_reboot on ARM */
#define RB_POWER_OFF 0x4321

void _start(void)
{
    register long r7 __asm__("r7") = SYS_reboot;
    register long r0 __asm__("r0") = RB_POWER_OFF;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r7) : "memory");
    for (;;) ;
}
