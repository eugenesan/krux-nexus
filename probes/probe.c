#include <linux/msm_mdp.h>
/* Deliberate size errors: the compiler prints the evaluated value, which is
 * how we extract sizeof() for a struct we cannot run code to measure. */
char probe_mdpc[sizeof(struct mdp_display_commit)];
char probe_msmfb[MSMFB_DISPLAY_COMMIT];
