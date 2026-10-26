#!/usr/bin/env python3
"""
apply-fragment.py -- set kconfig symbols in a Buildroot .config, then report
which ones actually took effect.

Why this exists instead of Buildroot's own support/kconfig/merge_config.sh:

    $ merge_config.sh -O out out/.config fragment
    No rule to make target 'alldefconfig'.  Stop.
    $ echo $?
    0

merge_config.sh calls `make alldefconfig` internally and does not check whether
it succeeded. It exits 0 having changed nothing, and a caller that trusts the
exit status -- which is the normal thing to do -- silently proceeds with the
base config. That is exactly the "reported success while the device was wrong"
failure this project has now hit nine times, this time in our own tooling.

So: apply the symbols directly, run olddefconfig, then READ BACK every symbol
the fragment asked for and report the ones that did not survive. No silent
passes either way.

Usage: apply-fragment.py <buildroot-dir> <build-dir> <fragment>
"""
import re
import subprocess
import sys
from pathlib import Path


def parse_fragment(path):
    """Return [(symbol, value)] for every `SYM=value` / `SYM=y` line."""
    out = []
    for line in Path(path).read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        m = re.match(r"^(BR2_[A-Za-z0-9_]+)=(.*)$", line)
        if m:
            out.append((m.group(1), m.group(2)))
    return out


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    buildroot, build, frag = sys.argv[1], sys.argv[2], sys.argv[3]
    cfg = Path(build) / ".config"
    if not cfg.exists():
        print(f"!! no .config at {cfg}", file=sys.stderr)
        return 1

    want = parse_fragment(frag)
    if not want:
        print(f"!! no symbols found in {frag}", file=sys.stderr)
        return 1

    text = cfg.read_text()
    applied, absent, added = [], [], []

    for sym, val in want:
        line = f"{sym}={val}"
        pat = re.compile(rf"^{re.escape(sym)}=.*$", re.M)
        if pat.search(text):
            text = pat.sub(line, text)
        else:
            # Not currently in .config. That is NOT the same as "no such
            # symbol": a fresh defconfig omits every option left at its
            # default, so most valid symbols are simply not listed. Append and
            # let olddefconfig be the judge -- if the symbol genuinely does
            # not exist, kconfig drops it and the read-back below reports it.
            text = text.rstrip("\n") + "\n" + line + "\n"
            added.append(sym)
        applied.append((sym, val))

    cfg.write_text(text)
    print(f"   wrote {len(applied)} symbols into .config")

    # Let kconfig resolve dependencies and drop anything invalid.
    r = subprocess.run(
        ["make", f"O={Path(build).resolve()}", "olddefconfig"],
        capture_output=True, text=True,
        cwd=buildroot,
    )
    if r.returncode != 0:
        print("!! olddefconfig failed", file=sys.stderr)
        print(r.stdout[-2000:], r.stderr[-2000:], file=sys.stderr)
        return 1

    # Read back. This is the point of the whole script.
    final = cfg.read_text()
    took, dropped = [], []
    for sym, val in applied:
        m = re.search(rf"^{re.escape(sym)}=(.*)$", final, re.M)
        if val == "n":
            # kconfig omits `=n` lines entirely. Absent therefore means n, which
            # is what we asked for. Only flag it if it somehow ended up set.
            if m is not None and m.group(1) not in ("n", ""):
                dropped.append((sym, val, f"still enabled as {m.group(1)}"))
            else:
                took.append((sym, val))
            continue
        if m is None:
            dropped.append((sym, val, "absent from .config after olddefconfig"))
        elif m.group(1) != val:
            dropped.append((sym, val, f"olddefconfig changed it to {m.group(1)}"))
        else:
            took.append((sym, val))

    print(f"   took effect: {len(took)}")
    if dropped:
        print(f"   !! DROPPED: {len(dropped)}")
        for sym, val, why in dropped:
            print(f"        {sym}={val}  -- {why}")
    if added:
        print(f"   appended (absent from .config, let kconfig judge): {len(added)}")
    if absent:
        print(f"   ?? no such symbol: {len(absent)}: {' '.join(absent)}")

    return 1 if dropped else 0


if __name__ == "__main__":
    sys.exit(main())
