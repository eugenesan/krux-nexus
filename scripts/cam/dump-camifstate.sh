#!/bin/bash
# Print the CAMIF update state and the stream bookkeeping, to see why
# ENABLE_CAMIF is not returned.
set -e
R=/home/user/Develop/src/android-air-gapped
T=$R/standalone/vendor/android_kernel_lge_hammerhead
cd $T
git checkout -q airgap/debug-capture

python3 - <<'PYEOF'
p = 'drivers/media/platform/msm/camera_v2/isp/msm_isp_axi_util.c'
s = open(p).read()
old = """\tcamif_update = msm_isp_get_camif_update_state(vfe_dev, stream_cfg_cmd);
"""
new = """\tcamif_update = msm_isp_get_camif_update_state(vfe_dev, stream_cfg_cmd);
\t/* DEBUG: what did the CAMIF state come out as? */
\t{
\t\tint k;
\t\tpr_err("DEBUG: camif_update %d, num_streams %u, cmd %u\\n",
\t\t\t(int)camif_update, stream_cfg_cmd->num_streams,
\t\t\tstream_cfg_cmd->cmd);
\t\tfor (k = 0; k < stream_cfg_cmd->num_streams; k++) {
\t\t\tstruct msm_vfe_axi_stream *si =
\t\t\t\t&axi_data->stream_info[HANDLE_TO_IDX(
\t\t\t\t\tstream_cfg_cmd->stream_handle[k])];
\t\t\tpr_err("DEBUG:   stream %d: src %d, pix_count %u, raw_count %u\\n",
\t\t\t\tk, si->stream_src,
\t\t\t\taxi_data->src_info[VFE_PIX_0].pix_stream_count,
\t\t\t\taxi_data->src_info[VFE_PIX_0].raw_stream_count);
\t\t}
\t}
"""
assert old in s, "anchor"
s = s.replace(old, new, 1)
open(p, 'w').write(s)
print("  the CAMIF update state is printed")
PYEOF

git add -A
git -c user.email=dev@local -c user.name=dev commit -q -m "debug: print the CAMIF update state and the stream bookkeeping"
git log --oneline -1 | sed 's/^/  /'
