#!/bin/bash
# Move the debug-only kernel changes onto a side branch and restore master.
set -e
R=/home/user/Develop/src/android-air-gapped
T=$R/standalone/vendor/android_kernel_lge_hammerhead
cd $T

echo "=== what is modified ==="
git status --short | sed 's/^/  /'

echo
echo "=== put the debug work on a side branch ==="
git branch -f airgap/debug-capture 2>/dev/null || true
git add -A
git -c user.email=dev@local -c user.name=dev commit -q -m "debug: capture the camera HAL's uploads (temporary)

Two debug-only changes, for lifting the chromatrix out of the vendor HAL
without parsing its blob:

  - kernel/printk.c: dmesg_restrict = 0, so that an unprivileged shell
    can read the kernel log
  - camera_v2/isp/msm_isp_util.c: msm_isp_proc_cmd() writes what the HAL
    uploaded (the reg_cfg_cmd blocks and the data blob) to
    /data/local/tmp/isp_cfg_<n>.bin

Neither belongs on master."
git branch -f airgap/debug-capture master
git log --oneline -2 | sed 's/^/  /'

echo
echo "=== restore master to the clean upstream + the two adb fixes ==="
git branch -f master 466f069d
git checkout -B master 466f069d
git cherry-pick 846ed801cb17c077f777e1a928128ec98c2271e3 >/dev/null
git cherry-pick e0cd9d48553194b5a2fa040f75eef2a282117a34 >/dev/null
git log --oneline -3 | sed 's/^/  /'
git status --short | sed 's/^/  (clean) /'
git branch -v | sed 's/^/  /'
