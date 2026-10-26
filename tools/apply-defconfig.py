#!/usr/bin/env python3
"""
apply-defconfig.py — device-agnostic Kconfig stripper for air-gapped images.

Turns a set of CONFIG symbols off in a kernel defconfig, then verifies that
nothing re-enabled them.  Works on any Linux kernel tree: the profile file is
just a list of symbol names.

    # dry run
    ./apply-defconfig.py arch/arm/configs/foo_defconfig profile.list

    # rewrite in place, and fail if any symbol came back after olddefconfig
    ./apply-defconfig.py arch/arm/configs/foo_defconfig profile.list -i --check

Why a script instead of a patch: every device has a differently-named set of
radio drivers, and a hand-written patch goes stale the moment the kernel
branch is rebased.  A profile is data, not a diff.

Defconfig semantics that matter (and that people get wrong):

  * "CONFIG_FOO=m" still means the driver ships in the image.  Modules are
    inserted at boot by ueventd/init and count as "present".  For an air gap
    you want "not set", not a module you never insmod.
  * A symbol can be re-enabled by `select` from another enabled symbol, and by
    `default y`.  Editing the defconfig is therefore necessary but NOT
    sufficient -- you must re-run olddefconfig and check the result.  That is
    what --check does.
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile

CONFIG_LINE = re.compile(r"^(CONFIG_[A-Za-z0-9_]+)=")
NOT_SET_LINE = re.compile(r"^# (CONFIG_[A-Za-z0-9_]+) is not set")


def read_profile(path):
    """Return an ordered list of symbol names from a profile file.

    One symbol per line.  Blank lines and # comments ignored.  Accepts bare
    names (FOO_BAR) or full CONFIG_ names.
    """
    syms, seen = [], set()
    with open(path) as fh:
        for raw in fh:
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            sym = line
            if "=" in sym:            # allow pasting lines straight out of a defconfig
                sym = CONFIG_LINE.match(sym).group(1)
            if not sym.startswith("CONFIG_"):
                sym = "CONFIG_" + sym
            if sym not in seen:
                seen.add(sym)
                syms.append(sym)
    return syms


def strip_defconfig(text, syms):
    """Force the symbols off by writing `# CONFIG_X is not set`.

    The previous version DELETED the line and relied on the symbol falling
    back to its Kconfig default. That is only safe when the default is `n`.
    For a symbol whose default is `y` it is the opposite of what it looks
    like -- `CONFIG_WLAN` is literally

        menuconfig WLAN
            depends on NET
            select WIRELESS
            default y

    so deleting its line asks for it, and kconfig turns Wi-Fi back on at the
    next config pass.

    Measured: of 79 profile symbols, 10 came back enabled this way --
    WLAN, WIRELESS, MSM_RMNET, MODEM_SUPPORT and six USB_NET_* variants.

    Writing `# ... is not set` forces `n` regardless of the default, which is
    what "not set" has to mean. A symbol genuinely *selected* by another
    enabled symbol can still be forced back on by kconfig; those need the
    selector removed as well, and audit_config() is what identifies them.
    """
    want = set(syms)
    out, hit = [], set()
    for line in text.splitlines():
        m = CONFIG_LINE.match(line)
        if m and m.group(1) in want:
            sym = m.group(1)
            if sym not in hit:
                out.append("# %s is not set" % sym)
            hit.add(sym)
            continue
        m = NOT_SET_LINE.match(line)
        if m and m.group(1) in want:
            hit.add(m.group(1))
            out.append(line)
            continue
        out.append(line)
    return "\n".join(out) + "\n", hit


def parse_choice_groups(kernel_root):
    """Map each CONFIG_ symbol to the other symbols in the same `choice` block.

    Returns {symbol: {symbol, ...}}, only for symbols that appear in a choice
    with at least one sibling.

    Why this is needed: writing `CONFIG_X=y` is NOT sufficient to turn on a
    choice value. kconfig resolves a choice by letting the LAST member it reads
    in .config win, and it does not error -- it prints

        .config:2730:warning: override: USB_G_ANDROID changes choice state

    and carries on. So `CONFIG_USB_FUNCTIONFS=y` written next to the existing
    `CONFIG_USB_G_ANDROID=y` is silently discarded, and the enable "fails"
    with no failure anywhere. To actually win the choice you must also
    explicitly clear the siblings.

    This is not hypothetical: CONFIG_USB_FUNCTIONFS is a value of the "USB
    Gadget Drivers" choice in drivers/usb/gadget/Kconfig (552..991), alongside
    USB_G_ANDROID, USB_ETH, USB_MASS_STORAGE and about twenty-five others. It is
    the only way adbd gets /dev/ffs.ep0.

    Only lines at column 0 are considered. That is sound for Kconfig: structural
    keywords (`config`, `choice`, `if`, ...) are unindented by definition, while
    properties (`depends on`, `select`, `default`, `bool`, `tristate`) are
    indented. Nested `choice` blocks are not supported and do not occur in this
    tree; a nested one would simply merge its members with the outer group,
    which over-clears rather than under-clears.
    """
    groups = {}
    for dirpath, dirnames, filenames in os.walk(kernel_root):
        dirnames[:] = [d for d in dirnames if d != ".git"]
        for name in filenames:
            if not name.startswith("Kconfig"):
                continue
            try:
                with open(os.path.join(dirpath, name), errors="replace") as fh:
                    lines = fh.read().splitlines()
            except OSError:
                continue
            stack = []
            for line in lines:
                if not line or line[0] in " \t#":
                    continue
                words = line.split()
                head = words[0]
                if head == "choice":
                    stack.append([])
                elif head == "endchoice":
                    if not stack:
                        continue
                    members = stack.pop()
                    if len(members) > 1:
                        for sym in members:
                            groups.setdefault(sym, set()).update(members)
                elif head in ("config", "menuconfig") and stack:
                    if len(words) > 1:
                        sym = words[1]
                        if not sym.startswith("CONFIG_"):
                            sym = "CONFIG_" + sym
                        if sym not in stack[-1]:
                            stack[-1].append(sym)
    return groups


def enable_symbols(text, syms, clear=()):
    """Force symbols ON, the counterpart to strip_defconfig.

    A profile can only remove. Some things must be ADDED to the air-gap kernel,
    most importantly CONFIG_USB_FUNCTIONFS: without it adbd starts, finds no
    /dev/ffs.ep0, and sits at `offline` forever with no way to see what the
    device is doing. Measured on all three existing kernels, which all have it
    off (Entry 55/53).

    `clear` names symbols that must be written as `# ... is not set`. It exists
    for choice siblings -- see parse_choice_groups(). Clearing them is what
    makes the enable stick.

    A symbol named in both the strip list and the enable list is a
    contradiction and is refused by main() rather than silently resolved.
    """
    up, down = set(syms), set(clear)
    down -= up                      # a symbol cannot be both set and cleared
    out, hit = [], []
    pending_up, pending_down = set(up), set(down)

    def emit(line):
        m = CONFIG_LINE.match(line)
        if m and m.group(1) in pending_up:
            pending_up.discard(m.group(1))
            out.append("%s=y" % m.group(1))
            hit.append(m.group(1))
            return
        m = NOT_SET_LINE.match(line)
        if m and m.group(1) in pending_up:
            pending_up.discard(m.group(1))
            out.append("%s=y" % m.group(1))
            hit.append(m.group(1))
            return
        m = CONFIG_LINE.match(line)
        if m and m.group(1) in pending_down:
            pending_down.discard(m.group(1))
            out.append("# %s is not set" % m.group(1))
            return
        m = NOT_SET_LINE.match(line)
        if m and m.group(1) in pending_down:
            pending_down.discard(m.group(1))
            out.append(line)
            return
        out.append(line)

    for line in text.splitlines():
        emit(line)
    # Any sibling not already present in the file still has to be stated, or
    # kconfig keeps whatever it had.
    for sym in sorted(pending_up):
        out.append("%s=y" % sym)
        hit.append(sym)
    for sym in sorted(pending_down):
        out.append("# %s is not set" % sym)
    return "\n".join(out) + "\n", hit


def audit_config(path, syms):
    """Return a list of symbols that are still enabled (=y or =m) in a .config."""
    enabled = []
    with open(path) as fh:
        for line in fh:
            m = CONFIG_LINE.match(line)
            if m and m.group(1) in set(syms):
                enabled.append("%s%s" % (m.group(1), line.split("=", 1)[1]))
    return enabled


def audit_forced(path, syms):
    """Return symbols that were required ON but are not =y after re-resolution.

    Dependencies can defeat an enable -- USB_FUNCTIONFS depends on EXPERIMENTAL,
    which depends on EXPERT -- so this is worth checking rather than assuming.
    """
    on = set()
    with open(path) as fh:
        for line in fh:
            m = CONFIG_LINE.match(line)
            if m:
                on.add(m.group(1))
    return [s for s in syms if s not in on]


def run_olddefconfig(kernel_root, defconfig_path, syms):
    """Run the config re-resolution step and report anything that came back.

    NOTE the target name: this is a 3.4 CAF tree, which has no `olddefconfig`
    -- that target arrived in ~3.16. Calling it aborted the whole profile step
    with "No rule to make target 'olddefconfig'", so `--config` could never
    complete. 3.4's equivalent is `silentoldconfig`; `oldconfig` would work but
    prompts interactively, and `yes '' | oldconfig` dies of SIGPIPE partway
    through, silently producing a truncated config.
    """
    # `O=` is essential. build-kernel.sh builds out-of-tree, so the config we
    # just wrote lives in the output directory, NOT at the kernel root. Without
    # O= make looks for <kernel_root>/.config, does not find it, and aborts
    # with '*** Configuration file ".config" not found!'.
    #
    # `oldconfig` with stdin closed, not `silentoldconfig`. This 3.4 CAF tree
    # has no `olddefconfig` target at all (it arrived in ~3.16), and its
    # `silentoldconfig` still PROMPTS for any symbol it has not seen -- which,
    # after a strip, is precisely the ones we removed. It then aborts with
    # "(NEW) aborted!". With stdin at EOF kconfig takes the default for every
    # prompt and finishes, exit 0. `yes ''` behaves identically here.
    outdir = os.path.dirname(os.path.abspath(defconfig_path)) or "."
    try:
        subprocess.run(
            ["make", "-C", kernel_root, "O=" + outdir, "ARCH=arm", "oldconfig"],
            check=True, capture_output=True, stdin=subprocess.DEVNULL,
        )
    except FileNotFoundError:
        sys.exit("error: make not found; re-run without --check and verify manually")
    except subprocess.CalledProcessError as exc:
        sys.exit("error: oldconfig failed:\n" + exc.stderr.decode(errors="replace"))

    produced = os.path.join(outdir, ".config")
    if not os.path.exists(produced):
        sys.exit("error: oldconfig produced no .config at " + produced)
    return audit_config(produced, syms)


def resolve_kernel_root(args):
    """Locate the kernel source tree.

    Explicit --kernel-root wins. Otherwise infer it from the defconfig path,
    which is always
        <root>/arch/arm/configs/<something>_defconfig
    so four levels up is the tree.
    """
    if args.kernel_root:
        return os.path.abspath(args.kernel_root)
    return os.path.abspath(
        os.path.join(os.path.dirname(args.defconfig), "..", "..", "..", "..")
    )


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("defconfig", help="path to the kernel defconfig")
    ap.add_argument("profile", help="file listing CONFIG symbols to disable")
    ap.add_argument("-i", "--in-place", action="store_true")
    ap.add_argument("--check", action="store_true",
                    help="after writing, run `make olddefconfig` and fail if any "
                         "symbol is still enabled (pass --kernel-root to enable)")
    ap.add_argument("--kernel-root", default=None,
                    help="kernel source root for --check (default: inferred)")
    ap.add_argument("--enable", action="append", default=[], metavar="SYMBOL",
                    help="symbol forced ON after stripping (e.g. "
                         "CONFIG_USB_FUNCTIONFS); repeatable")
    ap.add_argument("--disable", action="append", default=[], metavar="SYMBOL",
                    help="symbol forced OFF after stripping (e.g. "
                         "CONFIG_MT9M114, a sensor we do not have); repeatable")
    ap.add_argument("--allow", action="append", default=[], metavar="SYMBOL",
                    help="symbol that is allowed to remain enabled (e.g. "
                         "CONFIG_RFKILL); repeatable")
    args = ap.parse_args()

    if not os.path.exists(args.defconfig):
        sys.exit("no such defconfig: " + args.defconfig)

    syms = read_profile(args.profile)
    allow = set(
        (s if s.startswith("CONFIG_") else "CONFIG_" + s) for s in args.allow
    )

    with open(args.defconfig) as fh:
        original = fh.read()

    want_on = set(
        (s if s.startswith("CONFIG_") else "CONFIG_" + s) for s in args.enable
    )
    want_off = set(
        (s if s.startswith("CONFIG_") else "CONFIG_" + s) for s in args.disable
    )
    clash = sorted(want_on & (set(syms) | want_off))
    if clash:
        sys.exit("error: %s listed both as removed and as enabled" % ", ".join(clash))

    # --disable uses exactly the same mechanism as the strip profile: the
    # symbol is written as "# CONFIG_X is not set", which forces it off
    # whatever its Kconfig default is. It exists so that "we want this
    # sensor OFF because we do not have it" can be expressed next to "we want
    # that sensor ON", instead of the two lists drifting apart.
    off_all = sorted(set(syms) | want_off)

    patched, hit = strip_defconfig(original, off_all)
    if want_on:
        # Enabling a choice value requires clearing its siblings; otherwise
        # kconfig silently discards the request and only warns.
        groups = parse_choice_groups(resolve_kernel_root(args))
        siblings = set()
        for sym in want_on:
            for other in groups.get(sym, ()):
                if other != sym:
                    siblings.add(other)
        patched, forced = enable_symbols(patched, sorted(want_on), siblings)
        print("forced ON : %s" % ", ".join(forced))
        if siblings:
            print("choice siblings cleared (%d): %s"
                  % (len(siblings), ", ".join(sorted(siblings))))
    missing = [s for s in off_all if s not in hit]

    print("profile : %s (%d symbols)" % (args.profile, len(off_all)))
    if want_off:
        print("forced off: %s" % ", ".join(sorted(want_off)))
    print("defconfig: %s" % args.defconfig)
    print("removed : %d" % len(hit))
    if missing:
        print("NOT FOUND in defconfig (already off, or a different name on this "
              "device -- check it):")
        for s in missing:
            print("    %s" % s)
    if allow:
        print("allowed to stay on: %s" % ", ".join(sorted(allow)))

    if not args.in_place:
        sys.stdout.write(patched)
        sys.exit(0)

    if patched == original:
        print("no change; not writing")
    else:
        # Keep a .orig once, so you can always diff against the stock config.
        if not os.path.exists(args.defconfig + ".orig"):
            with open(args.defconfig + ".orig", "w") as fh:
                fh.write(original)
        with open(args.defconfig, "w") as fh:
            fh.write(patched)
        print("written: %s (backup at %s.orig)" % (args.defconfig, args.defconfig))

    if args.check:
        root = resolve_kernel_root(args)
        print("checking with oldconfig in %s ..." % root)
        leaked = run_olddefconfig(root, args.defconfig, off_all)
        if want_on:
            missing = audit_forced(os.path.join(
                os.path.dirname(os.path.abspath(args.defconfig)) or ".", ".config"),
                sorted(want_on))
            if missing:
                print("FAIL -- these were required ON but are not =y:")
                for m in missing:
                    print("    %s" % m)
                sys.exit(1)
        leaked = [s for s in leaked if s.split("=", 1)[0] not in allow]
        if leaked:
            print("\nFAIL -- these were re-enabled by `select`/`default`:")
            for s in leaked:
                print("    %s" % s)
            print("\nFind the selector with:")
            print("    grep -rn 'select %s' --include=Kconfig* ." % leaked[0].split("=")[0])
            sys.exit(1)
        print("OK -- no symbol came back enabled.")


if __name__ == "__main__":
    main()