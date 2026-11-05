# Linux 3.4 / MSM8974 Secure RAM Wipe via kexec Into Same Kernel

## Objective

Implement a high-assurance RAM wipe for a Nexus 5 (hammerhead, MSM8974) using:

- Existing kernel image only
- kexec
- Special command line: wipe=1
- No dependency on other partitions
- No second kernel image
- Final action: power off

Architecture:

```text
Running system
    ↓
shutdown command
    ↓
kexec same kernel image
    ↓
kernel boots with wipe=1
    ↓
early wipe path
    ↓
stop secondary CPUs
    ↓
disable interrupts
    ↓
disable DMA capable devices
    ↓
wipe all memory except reserved execution area
    ↓
flush caches
    ↓
power off
```

---

## Phase 1: Add Wipe Boot Mode

File:

```text
init/main.c
```

Add:

```c
static int wipe_mode;

static int __init wipe_setup(char *arg)
{
    wipe_mode = 1;
    return 1;
}

__setup("wipe=", wipe_setup);
```

Export:

```c
extern int wipe_mode;
```

---

## Phase 2: Add Early Hook

Recommended location:

```c
start_kernel()
```

Pseudo:

```c
asmlinkage void __init start_kernel(void)
{
    ...

    if (wipe_mode)
        wipe_main();

    ...
}
```

Alternative:

Hook immediately after:

```c
setup_arch();
parse_early_param();
```

Avoid waiting for userspace startup.

---

## Phase 3: Reserve Execution Area

Goal:

Do not wipe memory currently executing code.

Create section:

```c
__attribute__((section(".wipe")))
```

Or simply preserve:

```text
kernel text
kernel data
initrd if present
page tables
current stack
```

Obtain boundaries:

```c
extern char _stext[];
extern char _etext[];
extern char _sdata[];
extern char _end[];
```

Build exclusion range:

```c
wipe_reserved_start
wipe_reserved_end
```

Add safety padding:

```text
+16 MB
```

initially.

---

## Phase 4: Userspace Shutdown Script

```bash
#!/bin/sh

sync

echo 3 > /proc/sys/vm/drop_caches

swapoff -a

kexec -l /boot/zImage   --command-line="console=ttyHSL0,115200 wipe=1"

kexec -e
```

For Android-like environment:

```bash
kexec -l /proc/bootimg_kernel    --command-line="wipe=1"
```

if kernel image already available in memory.

---

## Phase 5: Wipe Entry Function

Create:

```c
void __noreturn wipe_main(void)
```

Sequence:

```c
void wipe_main(void)
{
    quiesce_system();
    wipe_memory();
    flush_everything();
    machine_power_off();

    while (1);
}
```

---

## Phase 6: Stop Other CPUs

Linux 3.4 ARM SMP.

Pseudo:

```c
static void stop_other_cpus(void)
{
    smp_send_stop();
}
```

Call before wiping.

Verification:

```c
num_online_cpus() == 1
```

---

## Phase 7: Disable Interrupts

```c
local_irq_disable();
local_fiq_disable();
```

Then:

```c
preempt_disable();
```

---

## Phase 8: Disable DMA Producers

Minimum targets on MSM8974:

```text
USB
KGSL GPU
WCNSS WiFi
Bluetooth
Camera
MDP display
Modem shared memory users
```

Pseudo:

```c
device_shutdown();
```

Then explicitly:

```c
msleep(100);
```

before wipe starts.

Goal: stop new RAM writes.

---

## Phase 9: Enumerate RAM

Use memblock.

```c
for_each_memblock(memory, reg)
```

Pseudo:

```c
for_each_memblock(memory, reg)
{
    wipe_region(reg->base,
                reg->size);
}
```

Skip reserved region.

---

## Phase 10: Range Exclusion Logic

```c
bool skip_range(start,end)
{
    return overlaps(start,end,
                    reserved_start,
                    reserved_end);
}
```

Split wipes around reserved area.

Example:

```text
RAM region
    ↓
part before kernel
part after kernel
```

both erased.

---

## Phase 11: Actual Wipe Routine

Safe version:

```c
static void wipe_chunk(void *p,size_t len)
{
    volatile u8 *q = p;

    while (len--)
        *q++ = 0;
}
```

Faster:

```c
memset(p,0,len);
```

Very fast option:

ARM NEON/STM assembly.

Not required initially.

---

## Phase 12: Cache Maintenance

After all writes:

```c
flush_cache_all();
```

If available:

```c
outer_flush_all();
```

Then:

```c
dsb();
isb();
```

ARM barriers.

---

## Phase 13: Verification Mode

Debug kernel option:

```text
wipe_verify=1
```

Sample pages:

```c
for(each region)
    verify first page
    verify middle page
    verify last page
```

Panic on mismatch.

---

## Phase 14: Progress Logging

Early implementation:

```c
printk("wipe start
");
printk("wipe region %pa size %pa
");
printk("wipe complete
");
```

Using serial console.

Remove in production.

---

## Phase 15: Power Off

Preferred:

```c
machine_power_off();
```

Fallback:

```c
kernel_power_off();
```

Final fallback:

```c
while (1)
    cpu_relax();
```

---

## Validation Plan

Stage 1

Wipe only 8 MB test range.

Stage 2

Wipe all memory except kernel reservation.

Stage 3

kexec into diagnostic kernel.

Instead of poweroff:

```text
wipe kernel
    ↓
kexec diagnostic kernel
    ↓
scan RAM
```

Verify zeros present.

Stage 4

Enable production poweroff.

---

## MSM8974 Specific Investigation Items

1. Verify CONFIG_KEXEC enabled.
2. Identify TrustZone reserved memory.
3. Identify modem shared-memory carveouts.
4. Verify memblock layout after kexec.
5. Verify outer cache implementation.
6. Verify reboot path preserves RAM until wipe kernel executes.
7. Verify device_shutdown() reaches camera, KGSL, USB and WCNSS devices.

---

## Expected Result

A single kernel image supports two modes:

```text
Normal boot
```

and

```text
wipe=1
```

The latter performs a nearly complete DRAM scrub and powers off without requiring any additional partitions or kernel images.
