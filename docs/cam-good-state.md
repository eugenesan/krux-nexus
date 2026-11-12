# Camera: the known-good state to come back to (2026-11-12)

**Use this before any change to the camera stream lifecycle.**  The
camera is working: the vendor processed preview (PIX_VIEWFINDER
through the CPP) has no stripes, the QR decoder reads codes, and the
entropy capture passes.  The next change (stopping the stream when no
client is watching) touches the fragile ISP/CPP start path, so this
records exactly what to restore if it goes wrong.

## Git revert points

| tree | commit / tag |
|---|---|
| `krux-nexus` (main) | tag `camera-good-2026-11-12` (commit `1d823a6`) |
| `krux` (submodule) | tag `camera-good-2026-11-12` (commit `e52f8f12`) |
| `linux` (submodule) | `cf9fd9c2edf` |
| `krux/firmware/micropython` | `ef4593e60` |
| `krux/vendor/embit` | `4df0f6f` |

To come back:

```sh
cd /home/user/Develop/src/krux-nexus
git checkout camera-good-2026-11-12
git submodule update --init --recursive
git -C krux checkout camera-good-2026-11-12
```

## The camera daemon

The deployed daemon and the checked-in build are the same binary:

```
md5  cam/camd                99ef6da3275fdb882efe9cbf18ed67fe
md5  /data/krux/camd         99ef6da3275fdb882efe9cbf18ed67fe
```

To rebuild from this source:

```sh
bash scripts/cam/build-camd.sh cam/camd
bash scripts/deploy-firmware.sh      # copies cam/camd to /data/krux
```

The camera runs the vendor processed preview (PIX_VIEWFINDER, full
sensor mode) and the stream runs continuously.  The binary above
(`99ef6da3...`) is the current one: the on-demand experiment was
reverted because a long sensor pause left the ISP/CPP out of frame
sync (see [cam-state.md](cam-state.md)).  This is again the build to
come back to.

## The boot images

The current pair (built 2026-11-12, with the backlight fix and the
on-demand stream):

| image | bytes | sha256 |
|---|---|---|
| `artifacts/built/boot-dev.img` | 20,203,520 | `d28822745a77e59926535eb12ac57aa7a585357cbaf604517c0358029b42e57c` |
| `artifacts/built/boot-standard-gzip.img` | 21,766,144 | `a1fce9463c86b3cd87911160fcfd24b35e02575ecc7468089e0945ad2fba47c2` |

The development image runs the firmware from `/data/krux`; the
standard image carries a read-only copy of it (`opt/krux`), verified to
be the same `camd` (md5 `99ef6da3...`), the same `pmu.py`, and the
P95 auto-exposure build of `micropython`/`camera.py`.

The scripts that check the camera, on the device:

```sh
# preview and QR
adb -s 0879524b0068abca shell 'cd /data/krux && ./micropython -c "import camtest; camtest.preview()"'
# the entropy capture (5.2 bits/px, dev 38 was the verified run)
adb -s 0879524b0068abca shell 'cd /data/krux && ./micropython -c "import camtest; camtest.entropy()"'
```

The full geometry and what was verified are in
[cam-state.md](cam-state.md).

## How to tell a broken change quickly

- `camd.log` shows `[isp] stream started` and keeps producing frames,
  or it shows `msm_isp_close_node: Invalid close`,
  `msm_csid_release: invalid state`, or `msm_vb2_buf_cleanup: NULL
  stream` — those mean the teardown path was taken and the node is in
  a bad state (a reboot is usually needed).
- A page that opens the camera and shows frozen or black frames means
  the ISP/CPP did not restart.
- If that happens: restore the source at the tag, rebuild `camd`,
  redeploy, and reboot the phone.
