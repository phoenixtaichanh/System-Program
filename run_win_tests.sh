#!/usr/bin/env bash
# ============================================================================
# run_win_tests.sh -- Run the compiled Windows test binaries and summarise.
#
# Expects the project to be built already (see build_msys2.sh).
# ============================================================================
export PATH="/ucrt64/bin:$PATH"

HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE" || exit 1

rc=0
for t in build/test_*.exe; do
    echo "---- $t ----"
    ./$t 2>&1 | grep -E 'Result|FAIL|skipped' | head -20
    [ "${PIPESTATUS[0]}" -ne 0 ] && rc=1
done

echo ""
if [ "$rc" -eq 0 ]; then
    echo "### ALL WINDOWS TESTS PASSED"
else
    echo "### SOME WINDOWS TESTS FAILED"
fi
exit "$rc"
