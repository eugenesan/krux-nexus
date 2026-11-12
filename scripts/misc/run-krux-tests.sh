#!/bin/bash
#
# Run the Krux test suite safely.
#
# Why this wrapper exists: one upstream test, the datum tool's
# "scan a QR" abort test, allocates without bound -- it grows past
# 1.5 GB and keeps going.  Run the suite without a limit on a 16 GB
# machine and the kernel's OOM killer takes the machine down with it.
# The same test is a known problem in the krux-mobile tree.
#
# So this wrapper does three things:
#
#   1. caps the address space (ulimit -v, default 2 GB), so a runaway
#      test fails with MemoryError instead of eating the machine;
#   2. leaves that one test out by default -- set KRUX_TEST_ALL=1 to
#      run it anyway (and expect it to fail);
#   3. skips the two test modules that need the "uUR" package, which is
#      not on PyPI and is not vendored here.
#
# Usage:
#   scripts/misc/run-krux-tests.sh [pytest arguments...]
#
#   KRUX_TEST_ALL=1     run the memory-hungry test as well
#   KRUX_MEM_LIMIT_MB   the address-space cap in MiB (default 2048)

set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
KRUX="$ROOT/krux"

MEM_LIMIT_MB="${KRUX_MEM_LIMIT_MB:-2048}"
# the address space is in KiB
ulimit -v $((MEM_LIMIT_MB * 1024)) 2>/dev/null || \
    echo "warning: could not set the memory limit" >&2

cd "$KRUX" || exit 1

# the vendored embit is not installed; the tests import it by path
export PYTHONPATH="$KRUX/vendor/embit/src${PYTHONPATH:+:$PYTHONPATH}"

IGNORE=(
    --ignore=tests/test_wallet.py
    --ignore=tests/pages/home_pages/test_wallet_descriptor.py
)
if [ "${KRUX_TEST_ALL:-0}" != "1" ]; then
    IGNORE+=(--deselect=tests/pages/test_datum_tool.py::test_datumtoolmenu_scan_qr_abort)
fi

echo "== the Krux test suite (memory limit ${MEM_LIMIT_MB} MiB)"
exec python3 -m pytest tests -q --tb=short -p no:cacheprovider "${IGNORE[@]}" "$@"
