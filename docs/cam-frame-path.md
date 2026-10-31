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
BOOT_IMG=/tmp/opencode/boot-fix-nocons.img \
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

## Kernel fix and 20 fps re-run (2026-10-30)

Patches on `airgap/cam-ksu` (`6a10b79fc03`):

- `MSM_CPP_DUMP_FRM_CMD = 0`.
- `msm_cpp_do_timeout_work` drops the timed-out frame and returns; no
  `msm_cpp_write()` and no resend from the workqueue.
- `msm_isp_process_stats_irq` guards against an unconfigured stats path.

Boot image: `/tmp/opencode/boot-fix-nocons.img`, kernel
`#1 SMP PREEMPT Fri Oct 30 14:15:25 MST 2026`.

Result of the 20 fps paced + Python run:

- `msm_cpp_dump_frame_cmd`: **0**
- `the frame timed out`: **0**
- `msm_isp_process_stats_irq` panic: **0**
- `Watchdog bark`: **0**
- `Unexpected IOMMU`: **0**
- `Write master ping pong mismatch`: **59** (warning only)

The first run used the unthrottled client (94 frames, 14.8 fps average;
groups 8.6-20.0). A throttled client (50 ms sleep, render every 3rd frame)
with the CPU governor pinned to `performance` gave:

- steady groups: **20.0 fps**
- typical groups: 18-20 fps
- worst group: 12.1 fps (`down=70.6 ms`)
- average: **~17.3 fps**

Python side (steady state, after the first-frame warm-up):

- `snapshot` 3-4 ms
- `find_qrcodes` 2-5 ms
- `lcd.display` 0-9 ms (every 3rd frame in the throttled test)
- loop work ~6-10 ms; **decode/render are not the bottleneck**

The remaining 20 fps gap is **camd downscale spikes** (35-40 ms typical,
70 ms spikes) plus the Y/C ping-pong warnings. The `interactive` governor
with a 300 MHz minimum was one contributor; `performance` helped but did
not remove the spikes.

Next tuning, in order:

1. Full-height CBCR **and a full-height chroma buffer** (`width*height`) to
   remove the ping-pong mismatch and the memory contention it implies.
2. NEON/128-bit downscale or cached ION mapping to remove the 70 ms spikes.
3. A blocking `snapshot` that waits for a new `latest_seq` (the current
   client can spin on duplicate frames; the throttled test approximates
   the right behavior with a 50 ms sleep).

## Full-height CBCR and the sustainable rate

### Full-height CBCR + full-height chroma buffer

`CAMD_FULL_CHROMA=1` programs `plane_cfg[1]` at full height and allocates
`width*height` for chroma:

- `Write master ping pong mismatch`: **0**
- IOMMU faults, CPP timeout, dump, watchdog: **0**
- but after ~24 frames the CPP stops accepting frames:
  `[cpp] CFG: Resource temporarily unavailable` (repeated) and
  `[client] no frame after 5s`.

So full-height CBCR fixes the ping-pong warning but is not viable with the
current captured CPP message. The default remains half-height.

### Sustainable CAMIF_RAW+CPP rate

| Target | Result |
|---|---|
| 20 fps | stalls at ~300 frames (~19 s) with `cpp CFG: EAGAIN`; no kernel fault |
| 15 fps | 410 frames over 30 s, no stall; groups 10.7-15.0, average **~13.7 fps**; downscale spikes 56-70 ms; 165 ping-pong warnings |

The CPP path sustains roughly **14-15 fps**, not 20. The 70 ms downscale
spikes occur even with no Python client, so they are camera/memory
contention, not client load.

### Fast-build re-measurement (2026-10-30 late)

`camd-fast` (the 3x3 wide-load build, `/tmp/opencode/camd-fast`, md5
`38e2beec8d5a7655632a80593203a238`) unpaced on a fresh camera boot with
the CPU governor at `performance`:

- **512 frames in 20 s (~25.6 fps)**, `down=31 ms`, no CPP stall, no
  watchdog;
- 225 `Write master ping pong mismatch` warnings (half-height chroma);
- no `pix0 overflow` during streaming.

The older `camd-paced` binary is the slow one (115 ms downscale,
~7.6 fps). The current-source build is faster (10 ms) but spins on
duplicate buffers when unpaced; `camd-fast` is the validated capture
binary. The 15 fps Python client can throttle this 25 fps capture.

### Consequence

The 20 fps target for 10 fps animated UR now requires the **PIX_VIEWFINDER
(VFE scaler) path** — the VFE overflow task — or a CPP throughput fix that
the captured vendor message does not expose. The 15 fps floor is enough for
5 fps UR, but not 10 fps.

## Conclusions

- The 0.6-0.8 fps was mostly **camd debug code**, then the **uncached 5x5
  CPU downscale**.
- With the debug path gated and the 3x3 wide-load downscale, camd runs at
  **22-28 fps** camd-only (603 frames/30 s).
- The kernel panic path is fixed: 20 fps camd + Python decode/render now
  runs without CPP dump, timeout, stats panic, watchdog or IOMMU faults.
- Full-height CBCR removes the ping-pong warning but stalls the CPP after
  ~24 frames; half-height remains the default.
- CAMIF_RAW+CPP sustains **~14-15 fps**, not 20: a 20 fps target stalls at
  ~300 frames, a 15 fps target survives 30 s at ~13.7 fps average.
- The 20 fps requirement therefore makes the **PIX_VIEWFINDER/VFE scaler**
  path (and its input-overflow fix) the critical next step, or the target
  must drop to 15 fps.
- Python decode/render is 6-10 ms per frame; it is not the bottleneck.
- The 3x3 center box is a speed/quality trade-off; if QR quality suffers,
  a 4x4 or 5x5 wide-load variant is the next step, not a return to scalar
  reads.
