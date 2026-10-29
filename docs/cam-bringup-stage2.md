# Camera bring-up — Stage 2: native POC frame capture

Paste this into a fresh session.  This stage is intentionally independent of
MicroPython and Krux.

## Goal

Produce a static ARM binary that opens the rear IMX179 and saves one
non-empty frame.  No Krux, no MicroPython, no Android framework.

## Prerequisite

Stage 1 is complete and the camera kernel boots with `/dev/video0`,
`/dev/video1` and `/dev/v4l-subdev11` present.

## Read first

1. `docs/cam-bringup-stage1.md`
2. `docs/plan-camera.md` §A2-A6
3. `docs/cam-state.md`
4. `cam/imaging8.c`, `cam/ion.h`, `cam/isp.h`, `cam/ispif.h`,
   `cam/sensor_live.h`, `cam/sensor_config.h`, `cam/cam_config.h`,
   `cam/cam_capture.h`, `cam/cam_cycle.h`, `cam/v4l2_caf.h`

## Implementation scope

Modify only the imaging lab tool:

- `cam/imaging8.c`
- its headers under `cam/`

Do not add Krux or MicroPython code in this stage.

## Required changes

Apply `docs/plan-camera.md` §A2 to `cam/imaging8.c`:

- print `planes[0..1].bytesused`, `.length`, `.data_offset`, and reservation fd;
- map each plane over its ION length, not `width*height`;
- count non-zero bytes and print a 16x16 sample grid;
- save `/data/local/tmp/frame0.nv12` with Y and UV sizes;
- print `pix_count`/`raw_count` from the VFE log.

Keep the processed `PIX_VIEWFINDER` path first.  Do not spend time on
`CAMIF_RAW` or CPP unless the processed path fails and the register
comparison is complete.

## Build

```sh
arm-linux-gnueabihf-gcc -O2 -static -march=armv7-a -mfpu=neon-vfpv3 \
    -mfloat-abi=hard -Icam -o /tmp/opencode/imaging-cam cam/imaging8.c -lpthread
adb push /tmp/opencode/imaging-cam /data/local/tmp/imaging
```

## Run

Use the processed path first:

```sh
adb shell 'mkdir -p /data/local/tmp'
adb shell 'cd /data/local/tmp && ./imaging \
  1640 1232 0x40000000 rear 0 - 1 binned cfg nocycle \
  0 0x2b 0 0 300 0 0 0 0x12 0 1 0xffffffff 0 0 0 1 0'
adb pull /data/local/tmp/frame0.nv12 /tmp/opencode/frame0.nv12
adb shell 'dmesg | grep -a CAMDBG' > /tmp/opencode/camdbg-run.txt
```

If the processed path succeeds, stop there.  Only if it fails, run the
three stream sources from `docs/plan-camera.md` §A3 and then the
instrumentation/vendor comparison from §A4/A5.

## Acceptance

- `planes[0].bytesused > 0`;
- Y plane is not all zero;
- `/tmp/opencode/frame0.nv12` exists and has the expected size;
- no kernel oops in `dmesg`;
- no `camif error status 0xd0`.

If those pass, Stage 2 is complete and Stage 3 may start.

## If it fails

Document the exact failing stage in `docs/cam-state.md`:
- no `process_done_buf`;
- `process_done_buf` but no `buf_done`;
- `buf_done` with a valid index but zero bytes;
- `pix_count=0` with CAMIF enabled;
- `camif error status 0xd0`.

Do not change the userspace design until the kernel comparison is recorded.
EOF
echo done