# Camera stability: software fault, not thermal

> **2026-10-30 late update.** The fast unpaced camd build produced
> 512 frames in 20 s (~25.6 fps) with no CPP stall or watchdog in one
> clean run. A 30 fps run reached the watchdog; the paced 15 fps path in
> the current source can starve the CPP with `msm_cpp_cfg` `EAGAIN`.
> Treat each camera boot+run as atomic: a failed run leaves the
> CPP/VFE wedged, and the next test must start from a fresh boot.
> The Python client now throttles to 15 fps while the daemon runs
> unpaced.

Date: 2026-10-30
Device: LG Nexus 5 (hammerhead), IMX179 rear camera, recovery ramdisk +
KernelSU camera kernel (`airgap/cam-ksu`, `ee54a78345f`), `fastboot boot` only.

## Question

Camera tests reboot the device after roughly 10-15 s of `camd`.  Is the
reboot caused by overheating (and the subsequent Android boots), or by our
camera software faulting the kernel?

## Answer

**Software/kernel fault, not thermal.**  The device crashes at 53 C with
`bootreason=wdog_bark`; no thermal trip appears in the kernel log, and the
bootloader never records `hw_thermal`/`sw_thermal`.  The camera's VFE/ISP
buffer management is broken in two related ways:

1. **Full-height NV12 chroma plane -> VFE IOMMU translation faults.**
   `plane_cfg[1].output_height` was the luma height (1232) while the chroma
   buffer is `width*height/2` (616 lines).  The CBCR write master ran past
   the buffer into unmapped IOVA (489-598 faults per run), and the ~10-line
   dump per fault could starve the watchdog.
2. **Half-height chroma -> write-master ping-pong mismatch.**  Programming
   the chroma at 616 removes the IOMMU faults, but the two write masters no
   longer complete in lockstep: `msm_isp_get_done_buf: Write master ping
   pong mismatch. Status: 0x10001` repeats every ~33 ms, the ISP tasklet
   queue overflows, the kernel stops logging, and the watchdog barks.  A
   fresh 90 s run crashed this way at **53 C**.

The half-height change is therefore a diagnostic workaround, not the final
fix; the VFE driver must keep the Y/C ping-pong bits paired while giving the
CBCR plane the buffer the hardware actually writes.

A third, independent printk storm can also reset the device: the CPP
timeout handler retries a stuck frame every 300 ms and dumps its 186-word
command each time (`MSM_CPP_DUMP_FRM_CMD`).  Heat from repeated Android
boots is a **consequence** of these resets, not their cause.

## Evidence

### 1. Reboot reason and saved kernel log

After a crash the device booted Android with:

```
/proc/cmdline: ... bootreason=wdog_bark
/proc/last_kmsg: 233062 bytes, 345 "Unexpected IOMMU page fault"
                 context = vfe0, FAR 0x009e7000..0x00ddd290
                 timestamps 60.849..61.117 s (0.268 s)
                 panic/oops/thermal markers: 0
```

The saved log contains **only** IOMMU fault dumps: the fault storm flooded
the ramconsole ring and pushed out everything before it.  `wdog_bark` is the
bootloader's record of a watchdog reset; a thermal shutdown would have been
recorded as `hw_thermal` or `sw_thermal`.

### 2. Thermal measurements at every stage

Sampler: `scripts/cam/thermal-sampler.sh` (every 2 s, `sync` after each
sample so the last samples survive a reset).  Host orchestration:
`scripts/cam/ae-thermal-test.sh`.

| Stage | Hottest zone | Result |
|---|---|---|
| Android after a crash, idle | 41 C | `bootreason=wdog_bark` |
| After 5-minute cooldown | 41 C | 11 samples, stable |
| Camera kernel boot (~13 s uptime) | 51 C | brief boot peak |
| After cooldown, before test | 44 C | camera kernel settled |
| AE test (`test_brightness3.py`, ~10 s) | **51 C** | survived; Y mean 2 -> 9 |
| 90 s continuous stream | **58 C** | survived; 42 frames |
| 30 s stream, chroma half-height | **57 C** | 18 frames, **0 IOMMU faults** |
| AE test, chroma half-height (~14 s) | **53 C** | **0 IOMMU faults**; Y 0 -> 10 -> 28 |
| 90 s stream, chroma half-height, reused boot | 48 C | **crashed at 6 s**: 5,106 CPP dump lines, 0 IOMMU faults |
| 90 s stream, chroma half-height, fresh boot, no serial console | **53 C** | **crashed at 4 s**: 12 ping-pong mismatches, 0 IOMMU faults, 0 CPP dumps |
| Camera kernel idle, 10 min, no camd | 49 C | no reboot; 0 new faults |
| Crash tail (previous boot) | not sampled | 345 faults / 0.268 s |

The two camera runs that produced hundreds of IOMMU faults stayed up at
51 C and 58 C.  Nothing in the thermal range observed here is close to a
shutdown point, and no thermal trip message appears in `dmesg` or
`last_kmsg`.

### 3. Fault signature

A normal 10 s run produced:

```
[  288.198] msm_vfe40_process_error_status: camif error status: 0x660
[  288.206] msm_iommu_v1: Unexpected IOMMU page fault!
[  288.207] msm_iommu_v1: name = vfe_iommu
[  288.208] msm_iommu_v1: context = vfe0 (0)
            FSR = 00000002 [TF]          (translation fault)
            FAR = 0x003e7000 ... 0x006f7660
```

489 faults in 10.5 s (~47/s) on that run; the device survived.  The 90 s run
kept 598 faults in the ring buffer, all in the last 8.6 s before teardown
(~70/s); it also survived.  Fault onset is therefore not deterministic: it
was ~1 s after stream start in one run and ~83 s in another.  At teardown
the kernel logs repeated:

```
msm_vfe40_process_error_status: camif error status: 0x660
msm_vfe40_process_error_status: image master 0 bus overflow
msm_vfe40_process_error_status: image master 1 bus overflow
msm_ispif_read_irq_status: VFE0 pix0 overflow.
```

The crash log's final 0.268 s contains 345 faults (~1287/s).  That is the
regime where the printk/IRQ storm starves the watchdog.  The difference
between a surviving run and a crash is the **fault rate**, not temperature.

### 4. The AE patch is not the trigger

Faults start **before** the MicroPython AE test connects:

```
288.198  camif error 0x660 + first IOMMU fault
288.206  first VFE IOMMU fault
288.4xx  camd starts streaming
289.47   AE test starts (supervisor.log)
```

Streaming without any sensor writes (90 s run, `TEST=-`) also faults.  The
AE commands only add register writes over the socket; they do not touch VFE
buffer setup.  The earlier successes and failures both occur with the same
capture path.

### 5. Idle control

The camera kernel was left up for 10 minutes with no `camd`
(`scripts/cam/ae-thermal-test.sh idle idle-10m 600`):

- no reboot (uptime 625 s -> 1248 s);
- zero new IOMMU faults (the `dmesg` count stayed at 598);
- hottest zone 49 C at the start, settling to 43 C.

So the kernel boot, KernelSU, and the recovery ramdisk are stable on their
own.  Camera streaming is required to trigger the fault.

### 6. Workaround: NV12 chroma plane height (not a complete fix)

`camd.c` programmed both planes of the NV12 stream with the luma height:

```c
scmd.plane_cfg[1].output_height = cbufs.height;      /* 1232 */
scmd.plane_cfg[1].output_scan_lines = cbufs.height;  /* 1232 */
```

but the chroma buffer is `width*height/2` (616 lines).  The VFE CBCR write
master therefore ran a second chroma plane's worth of data past the buffer,
into unmapped IOVA, producing the fault storm.

Experiment (same kernel and build, only this field changed):

| Run | Plane-1 height | Result |
|---|---|---|
| 10 s AE test | 1232 (old) | 489 faults, survived at 51 C |
| 90 s stream | 1232 (old) | 598 faults retained, survived at 58 C |
| 30 s stream | 616 | 18 frames, **0** faults, survived at 57 C |
| 14 s AE test | 616 | 7 frames, **0** faults, Y 0 -> 10 -> 28, 53 C |
| 90 s stream, fresh boot | 616 | **crashed at 53 C**: 12 ping-pong mismatches |

The half-height change is committed in `cam/camd.c` as a **workaround**: it
removes the IOMMU faults and makes short AE runs work, but it is not the
final fix.  With the half-height plane, a fresh 90 s run crashed at 53 C
with:

```
msm_isp_get_done_buf: Write master ping pong mismatch. Status: 0x10001
msm_isp_process_irq: Tasklet queue overflow: 0
```

repeated until the watchdog barked.  The two write masters no longer
complete in lockstep because their plane heights differ.  The full-height
plane keeps the ping-pong bits paired but overruns the half-height buffer.

Next experiment: keep the CBCR plane at full height **and** allocate a
full-height chroma buffer (`width*height`), so the hardware has the space it
writes and both write masters stay paired.

The workaround in `cam/camd.c`:

```c
scmd.plane_cfg[1].output_height = cbufs.height / 2;
scmd.plane_cfg[1].output_scan_lines = cbufs.height / 2;
```

`tools/ae-patch.py` was also repaired: its old pattern expected the V
handler to already exist, so `build-ae-camd.sh` could not apply/revert the
AE commands.  It now patches the clean source and leaves `sensor.py` alone
(that file already carries the implementation).

### 7. Second storm: CPP frame-command dump

With the chroma workaround, a 90 s run that reused the boot after two earlier
camd runs had **zero IOMMU faults** but still rebooted after 6 s
(`bootreason=wdog_bark`, hottest zone 48 C).  The saved log is 233 KB of:

```
MSM-CPP msm_cpp_dump_frame_cmd:1140 msg[000] = ...
...
MSM-CPP msm_cpp_dump_frame_cmd:1140 msg[185] = ...
```

`msm_cpp.c:87` hardcodes `MSM_CPP_DUMP_FRM_CMD 1`; the dump is called from
the CPP **timeout** handler (`msm_cpp_do_timeout_work`, lines 1214 and 1239),
not from the normal frame path.  `CPP_CMD_TIMEOUT_MS` is 300 ms, so a frame
that never completes is retried and its full 186-word command is dumped
every 300 ms.  5,106 lines in 2.7 s exceeds what `console=ttyHSL0,115200`
can drain, and the watchdog barks.

The 90 s run followed two earlier camd runs in the same boot, and teardown
had logged `MSM-CPP msm_cpp_delete_buff_queue:440 Queue not free`.  So the
CPP queue/state from the previous run is the likely reason the next run's
frames time out.  This is why a camera boot+run cycle must be atomic: use
`scripts/cam/ae-thermal-test.sh all ...` (boot, then run) rather than
reusing a boot for several runs.

A fresh-boot run with the serial console removed crashed differently — the
write-master ping-pong mismatch of section 6 — not with this dump.  So the
CPP dump and the ping-pong mismatch are independent failure modes; both
must be fixed before long runs are reliable.

Fixes:
- disable the debug dump: set `MSM_CPP_DUMP_FRM_CMD` to 0 and rebuild the
  camera kernel (or `echo 1 > /proc/sys/kernel/printk` as a runtime
  workaround);
- fix the CPP queue teardown so a second camd run starts clean;
- keep the atomic boot+run procedure until the teardown is fixed.

None of these changes the camera pipeline itself.

## Why the earlier conclusion was wrong

The previous investigation measured 5 -> 9 brightness after a 5-minute
cooldown, then saw reboots and attributed them to heat.  The reboots always
boot Android, which runs for several minutes at high CPU load and heats the
device.  That made the next test *start* warm, reinforcing the thermal
story.  But:

- a thermal shutdown is recorded by the bootloader as `hw_thermal` /
  `sw_thermal`; every observed reset is `wdog_bark`;
- the device survives identical camera load at 58 C, and the fresh 90 s
  crash happened at 53 C;
- the kernel log ends in VFE/ISP faults (IOMMU translation faults or
  write-master ping-pong mismatch), not a thermal trip;
- the fault starts before any AE write.

## Root cause and remaining fixes

Understood:
- **IOMMU translation faults**: the CBCR plane at full luma height overruns
  the half-height chroma buffer into unmapped IOVA.
- **Write-master ping-pong mismatch**: the half-height CBCR plane removes
  the IOMMU faults but breaks the Y/C completion pairing; the ISP tasklet
  queue overflows and the watchdog barks.  Crash measured at 53 C.
- **CPP timeout dump**: a stuck CPP frame is retried every 300 ms and its
  186-word command is dumped each time; a second printk storm.

Remaining fixes:
- Keep the CBCR plane at full height and allocate a full-height chroma
  buffer (`width*height`), or fix the VFE CBCR/ping-pong handling in
  `msm_isp_axi_util.c` / `msm_isp40.c`.  This is the blocker for sustained
  streaming.
- Disable `MSM_CPP_DUMP_FRM_CMD` and fix the CPP queue teardown
  (`msm_cpp_delete_buff_queue: Queue not free`).
- Check the remaining `camif error 0x660` input-side messages.

## Test procedure (changed by this investigation)

Use `scripts/cam/ae-thermal-test.sh`; do not launch `camd` from a foreground
`adb shell`.

```sh
# Full cycle: cool 5 min, boot camera kernel, run AE test, collect evidence.
BOOT_IMG=/tmp/opencode/boot-ksucam.img \
  scripts/cam/ae-thermal-test.sh all ae-bright-1 test_brightness3.py 20

# Streaming only (no sensor writes): isolates the AE patch.
scripts/cam/ae-thermal-test.sh all stream-only - 90

# Idle control: camera kernel, no camd.
scripts/cam/ae-thermal-test.sh idle idle-10m 600

# Inspect the collected evidence and print the verdict.
scripts/cam/ae-thermal-test.sh analyze ae-bright-1
```

Why the procedure changed:

- **Wait 10 s after adb appears.**  adbd/USB re-enumerates ~10-15 s after
  the ramdisk starts; a shell opened in that window is killed and takes its
  children with it.  The earlier harness lost its first run this way.
- **Run the supervisor detached (`setsid`) and poll for `DONE`.**  A
  foreground `adb shell` can die when adbd re-enumerates; that is not a
  kernel crash.  Polling `DONE` plus uptime distinguishes the two.
- **Sample every 2 s with `sync`.**  A watchdog reset does not flush the
  page cache; unsynced samples are lost.
- **Collect `last_kmsg` and `bootreason` immediately after Android comes
  back.**  They are the only record of the crashed boot.
- **Cool 5 minutes after an Android boot** before the next camera run, and
  record the samples, so thermal is ruled in or out with data.
