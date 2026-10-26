#include <linux/fb.h>
struct mdp_display_commit_stub { unsigned int flags; unsigned int wait; struct fb_var_screeninfo var; };
char probe_varsz[sizeof(struct fb_var_screeninfo)];
char probe_commitsz[sizeof(struct mdp_display_commit_stub)];
