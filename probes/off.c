#include <stdio.h>
#include <stddef.h>
#include <linux/fb.h>
int main(void){
    printf("  sizeof(fb_fix_screeninfo) = %zu\n", sizeof(struct fb_fix_screeninfo));
    printf("  offsetof smem_start  = %zu\n", offsetof(struct fb_fix_screeninfo, smem_start));
    printf("  offsetof smem_len    = %zu\n", offsetof(struct fb_fix_screeninfo, smem_len));
    printf("  offsetof line_length = %zu\n", offsetof(struct fb_fix_screeninfo, line_length));
    return 0;
}
