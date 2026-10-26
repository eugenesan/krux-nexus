#include <linux/fb.h>
char probe_total[sizeof(struct fb_var_screeninfo)];
char probe_var_after_flags[sizeof(((struct mdp_display_commit_stub *)0)->var)];
struct mdp_display_commit_stub { unsigned flags; unsigned wait; struct fb_var_screeninfo var; };
