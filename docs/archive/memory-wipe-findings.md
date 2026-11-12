# Memory-wipe findings

Scope: shutdown, secret lifetime, MicroPython facilities and a wipe plan for the
Nexus 5 (`hammerhead`). Bottom line: a software-only wipe can cover the MicroPython
process and framebuffer, but **cannot guarantee** that every copy in DRAM, kernel
caches, reserved memory or persistent storage is gone.

## Current shutdown path

1. UI entry points: Home menu `Shutdown` (`krux/src/krux/pages/home_pages/home.py:45,63`)
   and Login menu `Shutdown` (`krux/src/krux/pages/login.py:81-84`).
2. `Page.shutdown()` prompts, clears the display, draws `Shutting down…`, sleeps
   `SHUTDOWN_WAIT_TIME` = 300 ms (`krux/src/krux/pages/__init__.py:62,529-536`),
   then returns `MENU_SHUTDOWN`.
3. `Page.run()` turns that into `False` (`pages/__init__.py:538-541`); the Home loop
   breaks (`krux/src/boot.py:141-144`); `boot.py:172-173` calls `ctx.clear()` then
   `power_manager.shutdown()`.
4. `Context.clear()` only does `self.wallet = None; gc.collect()`
   (`krux/src/krux/context.py:44-47`). It does **not** zero memory.
5. `PowerManager.shutdown()` (`krux/src/krux/power.py:90-96`):
   `pmu.enable_adcs(False)`, then `pmu.enter_sleep_mode()`, then `machine.reset()`.
   `enter_sleep_mode()` writes `mem` to `/sys/power/state`
   (`krux/src/pmu.py:185-191`) — i.e. it **suspends first**; the reset only runs
   after a wake. `reboot()` uses the same `machine.reset()`
   (`krux/src/krux/power.py:98-101`).
6. `machine.reset()` (`krux/firmware/micropython/ports/unix/machine_hw_linux.c:327-337`)
   calls `machine_reset_do("shutdown")` (`:313-325`). That first makes a raw
   `syscall(SYS_reboot, 0x4321, 0x28121969)` with the `cmd`/`arg` arguments
   omitted (`:318`; ARM syscall 88), then `execve("/sbin/setprop", "sys.powerctl",
   "shutdown")` (`:321-324`). `execve` replaces the process and never returns; if it
   fails, `machine_reset()` loops forever (`:332-335`).
7. TWRP's init owns `on property:sys.powerctl=* / powerctl ${sys.powerctl}`
   (described at `machine_hw_linux.c:296-302`). `powerctl shutdown` enters the
   kernel's `RB_POWER_OFF` path: `kernel_power_off()` (`linux/kernel/sys.c:432-442`,
   `:507-509`). `pm_power_off` is `msm_power_off`
   (`linux/arch/arm/mach-msm/restart.c:382`); `__msm_power_off()` calls
   `pm8xxx_reset_pwr_off(0)`, `qpnp_pon_system_pwr_off(PON_POWER_OFF_SHUTDOWN)`,
   disables the SPMI arbiter and lowers PS_HOLD (`restart.c:175-196`). If PS_HOLD
   does not take, it waits 10 s and logs failure (`restart.c:192-194`).

`auto_shutdown` does **not** call `power_manager.shutdown()` on idle; it only dims
the backlight (`krux/src/krux/auto_shutdown.py:148-167`), so there is no idle wipe.

## Secret lifetime map

- `Key` keeps the mnemonic and passphrase as Python strings and the BIP32 root as an
  embit object: `krux/src/krux/key.py:159,162,178`. `extract_root()` calls
  `bip39.mnemonic_to_seed()` (`key.py:210-214`), which creates seed bytes
  (`krux/vendor/embit/src/embit/bip39.py:75-87`). `HDKey` stores an
  `ec.PrivateKey` whose `_secret` is the 32-byte private key
  (`.../bip32.py:17-56`; `.../ec.py:130-138`). `Wallet` wraps the key
  (`krux/src/krux/wallet.py:48-49`), and `ctx.wallet` holds it for the session
  (`krux/src/krux/context.py:41`).
- Temporary copies: word lists/entropy (`krux/src/krux/pages/mnemonic_loader.py:392-410,490`),
  SeedKeeper payloads (`krux/src/krux/pages/seedkeeper.py:762-767`), decrypted
  storage (`krux/src/krux/encryption.py:108-114`), QR bitmaps
  (`krux/src/krux/pages/qr_view.py:58-67`), HMAC key/ipad/opad (`krux/src/hmac.py`)
  and `_FastSHA512._data` (`krux/src/uhashlib_hw.py:26-40`) — all Python objects
  (`bytes`, `str`, `bytearray`, `int`) in the GC heap. Some strings are interned
  qstrs or frozen `.rodata`; the assembled mnemonic is a runtime heap object.
- Display: `Display.draw_qr_code()` (`krux/src/krux/display.py:512-525`) calls
  `lcd.draw_qr_code_binary()` (`krux/firmware/micropython/ports/unix/lcd_bcm.c:1084-1100`).
  Pixels live in the mmap'd framebuffer (`lcd_bcm.c:525-528`), which is
  **triple-buffered** 1080x5760 (`lcd_bcm.c:556-559`); the panel scans a copy in
  reserved memory (`lcd_bcm.c:576-581`). `lcd.clear()` only fills the current
  logical plane (`lcd_bcm.c:685-690`), so previous planes can retain a QR/mnemonic.
- Camera: `sensor.snapshot()` receives 480x640 NV12 bytes into Python objects
  (`krux/src/sensor.py:129-159`). The `camd` daemon keeps `latest_y`/`latest_uv`
  (`cam/camd.c:763-765`, allocated at `:1939-1941`), mmaps V4L2 buffers
  (`camd.c:1848-1851`) and allocates a per-frame copy (`camd.c:2172-2202`).
  `sensor.close()` does not stop `camd` (`sensor.py:165-178`), and
  `Camera.stop_sensor()` keeps the stream alive on hammerhead
  (`krux/src/krux/camera.py:488-496`).
- Flash: `settings.json` and `seeds.json` (`krux/src/krux/settings.py:35-36,160-184,280-303`);
  mnemonics are encrypted at rest (`krux/src/krux/encryption.py:41-114`).
  `flash.erase()` removes both files (`krux/src/flash/__init__.py:139-146`).
  Two extra artifacts matter: `/data/krux/last-qr.rgb565` is a raw frame written
  after a **failed** QR scan (`krux/src/krux/pages/qr_capture.py:300-305`) and can
  contain a photo of a seed QR; `/data/krux/fw.log` is truncated on start
  (`rootfs/init-stub.c:69,159`) but not on shutdown.

## Existing facilities

- `gc.collect()` is the only existing “clear” mechanism (`context.py:44-47`,
  `boot.py:150,169,172`). It frees blocks but does not zero them:
  `CLEAR_ON_SWEEP` is `0` (`krux/firmware/micropython/py/gc.c:52-54`), and the
  zeroing in the sweep is compiled out (`gc.c:731-736`). `gc_free()` also does not
  zero.
- No zeroization/wipe exists in `krux/src` or the port's C code. MaixPy
  (`krux/firmware/MaixPy`) is not checked out, so there is no K210-era wipe to reuse.
- `mbedtls_platform_zeroize()` exists (`krux/firmware/micropython/lib/mbedtls/library/platform_util.c:94-114`,
  volatile `memset` pointer at `:91`). mbedTLS is built
  (`ports/unix/mpconfigport.mk:31`); `crypto_fast.c` leaves its `out[32]`/`out[64]`
  stack buffers untouched (`ports/unix/crypto_fast.c:23-34,45-58`).
- GC heap access is C-only. `MICROPY_GC_SPLIT_HEAP` is `0` (`py/mpconfig.h:774-775`),
  so one `MP_STATE_MEM(area)` has `gc_alloc_table_start` … `gc_pool_end`
  (`py/mpstate.h:103-121`; layout `py/gc.c:146-190`). The unix port `malloc`s the
  heap and calls `gc_init()` (`ports/unix/main.c:472-475`); init-stub uses
  `-X heapsize=8M` (`rootfs/init-stub.c:315-320`). The Python `gc` module exposes
  only `collect`, `mem_free`, `mem_alloc`, `disable`, `enable`, `isenable`,
  `threshold` (`py/modgc.c:103-114`) — no heap address, so a new C entry point is
  required.
- Existing user-facing wipe: `FlashTools.erase_users_data()`
  (`krux/src/krux/pages/flash_tools.py:184-201`) → `erase_spiffs()` (`:172-182`) →
  `flash.erase()` (`krux/src/flash/__init__.py:139-146`). It removes two files, not RAM.

## Platform limits

- A user-space wipe reaches the MicroPython heap, stack and framebuffer. It does
  **not** reach kernel page cache, slab, zram/swap (`CONFIG_SWAP=y`,
  `CONFIG_ZRAM=y`, defconfig `:55,1337`), the persistent ram console
  (`CONFIG_ANDROID_RAM_CONSOLE=y`, `:3128`), ION/ISP/CPP/DMA buffers
  (`CONFIG_ION=y`, `:2446`), the display scanout copy, or modem/TZ/GPU memory.
- PMIC/PS_HOLD poweroff may leave DRAM contents physically present; cold-boot
  attacks can recover them. If PS_HOLD fails the device may remain powered
  (`restart.c:192-194`); there is no software guarantee of physical erasure.
- `execve("/sbin/setprop", ...)` tears down the old address space, but Linux does
  not zero freed anonymous pages before reuse. The C wipe must run **before** the
  raw syscall and before `execve`.
- This is a static armel binary with an 8 MiB GC heap. The init-stub is a service,
  not PID 1 (`rootfs/stub/init.recovery.service.rc:23`), so the firmware can only
  ask TWRP's init to power off; a kernel-level wipe needs kernel/init changes.
- `PowerManager.shutdown()` suspends via `/sys/power/state` before
  `machine.reset()` (`krux/src/krux/power.py:92-95`); if not woken, neither wipe nor
  poweroff happens.

## Recommended implementation

Ordered by effort/effect. Do not auto-erase persistent settings on every shutdown;
that breaks “load from storage”. The aim is best-effort zeroization of all
reachable RAM plus removal of debug artifacts.

1. **Fix shutdown ordering.** In `krux/src/krux/power.py:90-96`, remove
   `self.pmu.enter_sleep_mode()` from `shutdown()` (keep it for a separate sleep
   action). Then shutdown goes straight to `machine.reset()` instead of suspending.
2. **C-level GC-heap wipe (highest value, low effort).** Hook it at the top of
   `machine_reset_do()` in `krux/firmware/micropython/ports/unix/machine_hw_linux.c:313`,
   before line 318 and before `execve` at line 324. Add `#include "py/mpstate.h"`
   and `py/gc.h`. Add a compiler-safe helper, e.g.:
   ```c
   static void secure_zero(void *p, size_t n) {
       volatile unsigned char *v = (volatile unsigned char *)p;
       while (n--) *v++ = 0;
       __asm__ volatile("" : : "r"(p) : "memory");
   }
   static void wipe_gc_heap(void) {
       mp_state_mem_area_t *a = &MP_STATE_MEM(area);
       secure_zero(a->gc_alloc_table_start,
                   (size_t)(a->gc_pool_end - a->gc_alloc_table_start));
   }
   ```
   Split heap is off, so this covers the alloc/finaliser tables and every GC block
   (`py/gc.c:146-190`, `py/mpstate.h:103-121`): mnemonic strings, seed bytes, embit
   `PrivateKey._secret`, HMAC state, QR bitmaps, sensor frames and the settings
   dict. For split-heap builds, iterate `a = a->next`. After the wipe use only
   libc/static data (`argv`/`envp` are literals), never a Python API.
3. **Framebuffer wipe.** Add a non-static `void lcd_wipe_for_shutdown(void)` in
   `krux/firmware/micropython/ports/unix/lcd_bcm.c` (near `lcd_clear`, `:685`).
   If `lcd.ready`, lock `commit_mutex`, `secure_zero(lcd.fb, lcd.fb_size)` to cover
   **all three planes**, `msync()`, set `pan_offset = 0`, `dirty = 1`, and commit
   with `wait_for_finish = 1` (the current `lcd_do_commit_locked()` hardcodes `0`
   at `:187-190`). Sleep ~50 ms so the DSI copy completes. Declare it `extern` in
   `machine_hw_linux.c` and call it from `machine_reset_do()` **before** the heap
   wipe. This also overwrites the panel's scanout copy with black; the old copy is
   otherwise in reserved memory (`lcd_bcm.c:576-581`).
4. **Stack and C scratch wipe (best effort).** Zero `crypto_fast.c`'s `out[32]`
   and `out[64]` after the result is copied (`ports/unix/crypto_fast.c:23-34,45-58`)
   with `mbedtls_platform_zeroize()` or the helper above. For the Python/C stack,
   add a `machine_wipe_stack()` using a `volatile` local buffer and recursion (or,
   better, a `noreturn` function that switches to a small static alternate stack,
   zeroes the old stack range from the saved SP to the stack top, then `execve`s).
   Caveats: ordinary `memset` can be optimized away under `-O2`; use `volatile`
   and a compiler barrier (the mbedTLS pattern at `platform_util.c:91-114`). A
   simple recursive buffer only wipes memory below the current SP, not older
   frames; the alternate-stack version is needed for a real stack wipe.
5. **`camd` frame buffers.** Add a `SIGTERM`/`Q` handler in `cam/camd.c` that
   zeroes `latest_y`/`latest_uv` (`:763-765`) and every mmap'd V4L2 buffer
   (`:1848-1851`) before `munmap`/exit; signal it from the firmware before poweroff.
   Kernel ISP/CPP/ION buffers still cannot be wiped from user space.
6. **Persistent/debug data.** Remove or zero `/data/krux/last-qr.rgb565` on QR exit
   and shutdown (`krux/src/krux/pages/qr_capture.py:300-305`); it can be a photo of
   a seed QR. Truncate/remove `/data/krux/fw.log` on shutdown. Keep
   `Erase User's Data` explicit (`flash_tools.py:184-201`); `os.remove()` on
   ext4/f2fs does not guarantee physical erasure (journal, eMMC FTL). The encrypted
   `seeds.json` is safe at rest only if its key is not stored in flash.
7. **Defense in depth.** Optionally set `CLEAR_ON_SWEEP=1` (`py/gc.c:54`) so freed
   blocks are zeroed, but this is not a substitute for the final heap wipe. Disable
   zram/swap and the ram console for an air-gapped build if possible. Document that
   poweroff is not a physical DRAM erase; that threat model needs hardware
   secure-erase or a long power-off/tamper-resistant enclosure.

## Open questions

- Does TWRP init's `powerctl shutdown` stop services and unmount `/data`, or go
  straight to `reboot(RB_POWER_OFF)`? That decides whether `camd`/page-cache copies
  get flushed.
- Does writing `mem` to `/sys/power/state` actually suspend, and when does
  `machine.reset()` run after a wake?
- Can the raw `reboot(2)` at `machine_hw_linux.c:318` ever succeed, and with which
  `cmd` (the third argument is omitted)? Test before relying on setprop.
- Does `fix.smem_len` cover all three framebuffer planes (1080x5760)? Verify before
  zeroing only `lcd.fb_size`.
- Which C modules keep secret state outside the GC heap (`crypto_fast`, `uhashlib`,
  mbedTLS contexts; `secp256k1` is pure Python here)? Audit before claiming
  completeness.
- Is zram/swap actually enabled by TWRP's init on this image? Is the `seeds.json`
  key ever persisted, or only derived from the user PIN? Both affect at-rest claims.
- Can a stack wipe use `swapcontext()`/an alternate stack without disturbing the
  init/powerctl handoff? Prototype before shipping.
