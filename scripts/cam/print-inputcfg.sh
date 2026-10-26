#!/bin/bash
# Print the camera HAL's ISP input configuration (the CAMIF geometry and
# the pixel clock) from inside the driver, where the reads work.
set -e
R=/home/user/Develop/src/android-air-gapped
T=$R/standalone/vendor/android_kernel_lge_hammerhead
cd $T
git checkout -q airgap/debug-capture

python3 - <<'PYEOF'
p = 'drivers/media/platform/msm/camera_v2/isp/msm_isp_util.c'
s = open(p).read()
# remove the file capture (it cannot read the user's buffer here)
start = s.find("\t\t/* DEBUG: capture the camera HAL's input configuration */")
if start >= 0:
    end = s.index("\t\tmutex_lock(&vfe_dev->core_mutex);", start)
    s = s[:start] + s[end:]
    print("  the file capture removed")

old = """\tstruct msm_vfe_input_cfg *input_cfg = arg;

\tswitch (input_cfg->input_src) {
"""
new = """\tstruct msm_vfe_input_cfg *input_cfg = arg;

\t/* DEBUG: what does the camera HAL ask for? */
\tpr_err("DEBUG: ISP input: src %d, pix_clk %u, mux %d, camif_input %d\\n",
\t\tinput_cfg->input_src, input_cfg->input_pix_clk,
\t\tinput_cfg->d.pix_cfg.input_mux,
\t\tinput_cfg->d.pix_cfg.camif_cfg.camif_input);
\tpr_err("DEBUG:   camif: lines %u, pixels %u, first_px %u, last_px %u, first_line %u, last_line %u\\n",
\t\tinput_cfg->d.pix_cfg.camif_cfg.lines_per_frame,
\t\tinput_cfg->d.pix_cfg.camif_cfg.pixels_per_line,
\t\tinput_cfg->d.pix_cfg.camif_cfg.first_pixel,
\t\tinput_cfg->d.pix_cfg.camif_cfg.last_pixel,
\t\tinput_cfg->d.pix_cfg.camif_cfg.first_line,
\t\tinput_cfg->d.pix_cfg.camif_cfg.last_line);

\tswitch (input_cfg->input_src) {
"""
assert old in s, "anchor"
s = s.replace(old, new, 1)
open(p, 'w').write(s)
print("  the HAL's input configuration is printed")
PYEOF

git add -A
git -c user.email=dev@local -c user.name=dev commit -q -m "debug: print the camera HAL's ISP input configuration

The CAMIF geometry and the pixel clock the vendor uses, printed from
inside the driver (the user's buffer cannot be copied out here)."
git log --oneline -1 | sed 's/^/  /'
