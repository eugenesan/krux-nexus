#include <stdio.h>
#include <sys/auxv.h>
#ifndef AT_HWCAP
#define AT_HWCAP 16
#endif
int main(void) {
    unsigned long h = getauxval(AT_HWCAP);
    printf("AT_HWCAP = 0x%lx\n", h);
    printf("  HWCAP_FP    (bit 11) : %s\n", (h & (1u<<11)) ? "YES" : "NO");
    printf("  HWCAP_ASIMD (bit 12) : %s\n", (h & (1u<<12)) ? "YES" : "NO");
    return 0;
}
