# Camera frame-path profile (2026-10-30)

## Goal

Find where the ~0.6-0.8 fps went, and determine whether the
CAMIF_RAW+CPP path can reach the animated-UR target of
**20 fps capture / 15 fps decode**.

## Method

Two runtime controls were added to `cam/camd.c`:

| Variable | Meaning |
|---|---|
| `CAMD_DEBUG_FRAMES=1` | Enable the old debug path: 300 ms settle, scan every buffer, write `frame*.nv12`, 100 ms sleep. Default **off**. |
| `CAMD_PROFILE=1` | Print a 10-frame stage summary. `2` also prints every frame. |
| `CAMD_BENCH=1` | One-shot read-bandwidth benchmark on the ION buffer. |
| `CAMD_TARGET_FPS=N` | Pace the capture loop to N fps (0 = unpaced). |

Stages timed with `CLOCK_MONOTONIC`:

```
loop start
  update_stream + config_done
  cpp_send_chromatrix
  isp_replay(cam_cycle)
  poll + DQBUF
  debug block
  QVGA downscale
  QBUF
  pace
```

The debug block was the first surprise: it slept **400 ms** and read/wrote
~18 MB **per frame**, so it alone capped the loop at ~2 fps and was the main
reason the old runs looked like 0.6-0.8 fps.

## Results

### 1. Debug path removed, 5x5 scalar downscale

| Stage | ms |
|---|---|
| update + config_done | 0.01 |
| cpp chromatrix | 0.4-0.6 |
| replay (nocycle) | 0.00 |
| poll / DQBUF | 0.0-6.0 |
| DQBUF | 0.01-0.1 |
| debug | 0.0 |
| **5x5 downscale** | **187-250** |
| QBUF | 0.01 |
| **total** | **188-260 ms → 3.9-5.6 fps** |

The camera/CPP side was already fast; the CPU downscale was the bottleneck.

### 2. ION read bandwidth

The frame buffers are mapped uncached, so access width matters:

| Read | Time for 2.02 MB Y | Bandwidth |
|---|---|---|
| 8-bit scalar | 192.4 ms | 10.5 MB/s |
| 32-bit | 45.3 ms | 44.6 MB/s |
| 64-bit | 24.3 ms | 83.2 MB/s |
| `memcpy` | 17.8 ms | 113.6 MB/s |

### 3. 3x3 center box with 32/64-bit loads

The old 5x5 scalar box was replaced by a 3x3 center box of each 5x5 block:
9 of 25 pixels, but read with one 32-bit load per row (Y) and one 64-bit
load per row (UV). The skipped ring is the outer 2 pixels of each block.

| Stage | ms (typical) |
|---|---|
| update + cpp + dq + q | ~0.6 |
| poll / DQBUF | 0.0-0.2 |
| **downscale** | **34-44** (spikes 54-85) |
| **total** | **36-45 ms → 22-28 fps** |

A 30 s foreground run produced **603 frames** and survived; the hottest
zone stayed in the normal 50s C range. The camera/CPP `wait` is essentially
zero, so the path can deliver frames faster than the 20 fps target.

## New blocker: CPP timeout → ISP stats panic

The CPU side now meets 20 fps, but the **camera driver is not stable under
load**. A paced 20 fps run with the Python decoder sharing the CPU panicked:

```
msm_cpp_dump_frame_cmd: ... msg[000..265] ...      (380 lines)
cpp_timer_callback called ...
fatal: cpp_timer expired for identity=... frame_id=...
the frame timed out; not reloading the firmware
...
msm_isp_process_stats_irq+0x0/0x1f0
Rebooting in 5 seconds..
Last boot reason: reboot
```

The timeout handler (`msm_cpp_do_timeout_work`) still writes the frame
command back to the CPP from a workqueue, although its own comment says
that path "takes the whole kernel down" on this device. The same handler
prints all 186 words of the frame command on every retry
(`MSM_CPP_DUMP_FRM_CMD 1`), which also floods the console.

So the remaining blockers are kernel-side, not CPU-side:

1. `MSM_CPP_DUMP_FRM_CMD` must be 0.
2. The timeout handler must **drop the frame and return** after logging;
   it must not `msm_cpp_write()` or resend from the workqueue.
3. The ISP stats IRQ must not run against an unconfigured/expired stream
   on the CAMIF_RAW path (mask it if stats are not used).
4. The underlying CPP timeout needs a real fix (queue depth/pacing and/or
   the VFE CBCR ping-pong mismatch).

Until those are fixed, the Python decode/render profile cannot run to
completion; the kernel panics first.

## How to run

```sh
# Fresh camera boot (new camd process per boot; do not reuse a dirty CPP).
BOOT_IMG=/tmp/opencode/boot-nocons.img \
  scripts/cam/ae-thermal-test.sh boot

adb push /tmp/opencode/camd-paced /data/krux/camd-paced
adb shell 'chmod 755 /data/krux/camd-paced; sync'

# Per-frame profile, paced at 20 fps.
adb shell 'cd /data/krux && CAMD_DEBUG_FRAMES=0 CAMD_PROFILE=2 \
  CAMD_TARGET_FPS=20 /data/krux/camd-paced 1640 1232 0x40000000 rear 0 \
  /data/krux/cpp_firmware_v1_2_0.fw 2 binned cfg nocycle 0 0x2b 0 0 300 \
  0 0 0 0x12 0 1 0xffffffff 0 0 0 1 0'
```

`CAMD_BENCH=1` prints the 8/32/64-bit and memcpy bandwidth once.  
`CAMD_DEBUG_FRAMES=1` restores the old buffer scans/dumps/sleeps.  
Always run the profiler in the foreground or through a separate adb stream
so the timing lines survive a reset; a log on `/data` may lose its tail when
the watchdog fires.

## Conclusions

- The 0.6-0.8 fps was mostly **camd debug code**, then the **uncached 5x5
  CPU downscale**.
- With the debug path gated and the 3x3 wide-load downscale, camd runs at
  **22-28 fps** (603 frames/30 s), which clears the 20 fps capture target.
- The camera/CPP stage latency is not the bottleneck (`poll` ~0-6 ms).
- The next work is kernel stability: disable the CPP dump, stop the unsafe
  timeout resend, fix/mask the stats IRQ, then re-run the 20 fps profile
  together with the Python `snapshot + find_qrcodes + render` loop.
- The 3x3 center box is a speed/quality trade-off; if QR quality suffers,
  a 4x4 or 5x5 wide-load variant is the next step, not a return to scalar
  reads.
