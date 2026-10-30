#!/usr/bin/env python3
"""cam-evidence.py -- summarize one ae-thermal-test.sh evidence directory.

Reads the files the harness collects (thermal.log, supervisor.log,
camd.log, test.log, last_kmsg.txt, cmdline.txt, bootreason.txt) and prints
a short, evidence-backed verdict about why the device rebooted.

Usage: tools/cam-evidence.py /tmp/opencode/cam-evidence/<label>
"""

import os
import re
import sys


def read(path, default=""):
    try:
        with open(path, "r", errors="replace") as f:
            return f.read()
    except OSError:
        return default


def norm_temp(raw):
    """Normalise a thermal zone value.

    tsens zones report degrees Celsius; PMIC/misc zones report
    millidegrees; absent sensors report -40.  Return None for absent.
    """
    try:
        v = float(raw)
    except (TypeError, ValueError):
        return None
    if v == -40.0:
        return None
    if v > 1000.0 or v < -1000.0:
        v /= 1000.0
    if v < -20.0 or v > 200.0:  # implausible after scaling
        return None
    return v


def parse_thermal(path):
    text = read(path)
    lines = [l for l in text.splitlines() if l.strip()]
    if not lines:
        return None
    header = lines[0].split()
    if not header or header[0] != "t_unix":
        return None
    zones = []
    for name in header[2:]:
        if name in ("load1", "camd"):
            break
        zones.append(name)
    rows = []
    for line in lines[1:]:
        parts = line.split()
        if len(parts) < 2 + len(zones) + 2:
            continue
        try:
            t = int(parts[0])
            up = float(parts[1])
        except ValueError:
            continue
        temps = {}
        maxv = None
        for i, z in enumerate(zones):
            v = norm_temp(parts[2 + i])
            if v is not None:
                temps[z] = v
                if maxv is None or v > maxv:
                    maxv = v
        try:
            load = float(parts[2 + len(zones)])
            camd = int(parts[3 + len(zones)])
        except (ValueError, IndexError):
            load, camd = 0.0, 0
        rows.append({"t": t, "up": up, "temps": temps, "max": maxv,
                     "load": load, "camd": camd})
    return {"zones": zones, "rows": rows}


def summarize_thermal(name, th):
    if not th or not th["rows"]:
        print(f"thermal[{name}]: no samples")
        return None
    rows = th["rows"]
    active = [r for r in rows if r["camd"] > 0]
    vals = [r["max"] for r in rows if r["max"] is not None]
    avals = [r["max"] for r in active if r["max"] is not None]
    first, last = rows[0], rows[-1]
    print(f"thermal[{name}]: {len(rows)} samples, "
          f"uptime {first['up']:.0f}s..{last['up']:.0f}s, "
          f"camd-active samples={len(active)}")
    if vals:
        print(f"  max overall={max(vals):.1f}C")
    if avals:
        hot = max(active, key=lambda r: r["max"] if r["max"] is not None else -999)
        print(f"  max while camd active={max(avals):.1f}C "
              f"(uptime {hot['up']:.0f}s, load {hot['load']:.2f})")
    # Last three samples: what the device looked like immediately before
    # the log ended (i.e. before the watchdog reset).
    print("  last samples:")
    for r in rows[-3:]:
        tz = ", ".join(f"{z}={r['temps'][z]:.0f}" for z in
                       ("tsens_tz_sensor6", "tsens_tz_sensor8", "pm8841_tz")
                       if z in r["temps"])
        print(f"    up={r['up']:.0f}s max={r['max'] if r['max'] is not None else -1:.0f}C "
              f"camd={r['camd']} load={r['load']:.2f} {tz}")
    return max(vals) if vals else None


def parse_supervisor(path):
    text = read(path)
    marks = []
    for line in text.splitlines():
        m = re.match(r"(\d+) up=([\d.]+) (.*)", line)
        if m:
            marks.append({"t": int(m.group(1)), "up": float(m.group(2)),
                          "msg": m.group(3)})
    return marks


def summarize_kmsg(path):
    text = read(path)
    faults = text.count("Unexpected IOMMU page fault")
    ctx = re.findall(r"msm_iommu_v1: context = ([A-Za-z0-9_]+)", text)
    fars = [int(x, 16) for x in
            re.findall(r"msm_iommu_v1: FAR\s*=\s*([0-9a-fA-F]+)", text)]
    stamps = [float(x) for x in re.findall(r"^\[\s*(\d+\.\d+)\]", text,
                                           re.MULTILINE)]
    panics = re.findall(r"(\bKernel panic\b|\bOops\b|\bUnable to handle\b|"
                        r"\bBUG:|\bsoft lockup\b|\bhung task\b|"
                        r"thermal.*trip|Thermal.*shutdown|\bWatchdog bark\b)",
                        text, re.IGNORECASE)
    cpp_dumps = text.count("msm_cpp_dump_frame_cmd")
    pix0 = text.count("VFE0 pix0 overflow")
    pingpong = text.count("Write master ping pong mismatch")
    tasklet = text.count("Tasklet queue overflow")
    print(f"last_kmsg: {len(text)} bytes, "
          f"IOMMU faults={faults}, pingpong={pingpong}, "
          f"tasklet_overflow={tasklet}, cpp_dump_lines={cpp_dumps}, "
          f"pix0_overflow={pix0}, panic/oops/thermal markers={len(panics)}")
    if ctx:
        uniq = {}
        for c in ctx:
            uniq[c] = uniq.get(c, 0) + 1
        print("  IOMMU contexts: " + ", ".join(f"{k}={v}" for k, v in
                                               sorted(uniq.items())))
    if fars:
        print(f"  FAR range: 0x{min(fars):08x}..0x{max(fars):08x}")
    if stamps:
        print(f"  kmsg timestamps: {min(stamps):.3f}s..{max(stamps):.3f}s "
              f"({max(stamps) - min(stamps):.3f}s span)")
    for p in panics[:8]:
        print(f"  marker: {p}")
    return {"faults": faults, "contexts": ctx, "fars": fars,
            "panics": panics, "bytes": len(text), "cpp_dumps": cpp_dumps,
            "pix0": pix0, "pingpong": pingpong, "tasklet": tasklet}


def bootreason(ev):
    cmdline = read(os.path.join(ev, "cmdline.txt"))
    prop = read(os.path.join(ev, "bootreason.txt")).strip()
    m = re.search(r"\bbootreason=([^\s]+)", cmdline)
    cmd = m.group(1) if m else "(none)"
    print(f"reboot reason: cmdline={cmd} prop={prop or '(none)'}")
    return cmd, prop


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    ev = sys.argv[1]
    if not os.path.isdir(ev):
        print(f"no such evidence directory: {ev}")
        return 2
    print(f"=== evidence: {ev} ===")
    reason_cmd, reason_prop = bootreason(ev)

    th = parse_thermal(os.path.join(ev, "thermal.log"))
    max_temp = summarize_thermal("run", th)

    marks = parse_supervisor(os.path.join(ev, "supervisor.log"))
    if marks:
        print("supervisor landmarks:")
        for m in marks:
            print(f"  up={m['up']:.0f}s {m['msg']}")
    else:
        print("supervisor landmarks: none (kernel died before/while starting camd?)")

    test = read(os.path.join(ev, "test.log")).strip()
    if test:
        print("test output (tail):")
        for line in test.splitlines()[-8:]:
            print(f"  {line}")
    else:
        print("test output: none")

    km = summarize_kmsg(os.path.join(ev, "last_kmsg.txt"))
    idle_ok = read(os.path.join(ev, "idle-ok.txt")).strip()
    run_ok = read(os.path.join(ev, "run-ok.txt")).strip()

    print("\n=== verdict ===")
    if run_ok:
        print(f"RUN COMPLETED WITHOUT A REBOOT: {run_ok}")
        if max_temp is not None:
            print(f"  Hottest zone during the run: {max_temp:.1f}C.")
        return 0
    if idle_ok:
        print(f"IDLE CONTROL COMPLETED: {idle_ok}")
        print("  The camera kernel stayed up with no camd running, so the "
              "boot/kernel alone is not enough to trigger the reboot.")
        if max_temp is not None:
            print(f"  Hottest zone during the idle run: {max_temp:.1f}C.")
        return 0
    rebooted = reason_cmd not in ("(none)",) or not os.path.exists(
        os.path.join(ev, "DONE"))
    if km["bytes"] == 0:
        print("No last_kmsg captured; cannot attribute the reboot from kernel logs.")
    elif km["faults"] > 0:
        print(f"SOFTWARE/KERNEL FAULT: {km['faults']} VFE IOMMU translation "
              f"faults on context(s) {', '.join(sorted(set(km['contexts'])))} "
              f"precede the reboot.")
        print("  The VFE (camera ISP) wrote to an unmapped IOVA; the resulting "
              "interrupt/printk storm ended in a watchdog bark.")
    elif km["pingpong"] > 0:
        print(f"SOFTWARE/KERNEL FAULT: {km['pingpong']} VFE write-master "
              f"ping-pong mismatches ({km['tasklet']} tasklet overflows) "
              f"before the reboot.")
        print("  The Y and C write masters are not completing in lockstep; "
              "the ISP tasklet queue overflows and the watchdog barks.")
    elif km["cpp_dumps"] > 100:
        print(f"SOFTWARE/KERNEL FAULT: the CPP debug dump printed "
              f"{km['cpp_dumps']} lines of frame-command hex before the "
              f"reboot (186 lines/frame, MSM_CPP_DUMP_FRM_CMD).")
        print("  The printk storm starves the watchdog.  Disable the dump "
              "(set MSM_CPP_DUMP_FRM_CMD to 0) or raise console_loglevel.")
    elif km["panics"]:
        print("Kernel reported a panic/oops/lockup marker; see markers above.")
    else:
        print("last_kmsg shows no IOMMU fault, ping-pong mismatch, CPP dump, "
              "panic or oops; inspect dmesg.")

    if max_temp is not None:
        if max_temp < 70.0:
            print(f"THERMAL NOT IMPLICATED: hottest zone was {max_temp:.1f}C, "
                  f"far below any trip/shutdown point.")
        elif max_temp < 85.0:
            print(f"THERMAL UNLIKELY: hottest zone was {max_temp:.1f}C.")
        else:
            print(f"THERMAL PLAUSIBLE: hottest zone was {max_temp:.1f}C; "
                  f"check trip points.")
    if "wdog" in reason_cmd or "wdog" in reason_prop:
        print("Reboot reason is a watchdog bark, not a thermal shutdown "
              "(thermal shutdown would report hw_thermal/sw_thermal).")
    if not rebooted:
        print("Run completed without a reboot (DONE marker present).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
