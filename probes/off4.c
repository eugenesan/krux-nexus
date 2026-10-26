#include <linux/fb.h>
/* Emit each field offset as a symbol-sized array so nm reveals the layout
 * the ARM compiler actually produces. */
#define P(f) char probe_##f[__builtin_offsetof(struct fb_fix_screeninfo, f)]
P(id); P(smem_start); P(smem_len); P(type); P(visual);
P(xpanstep); P(ywrapstep); P(line_length); P(mmio_start); P(accel); P(capabilities);
char probe_total[sizeof(struct fb_fix_screeninfo)];
