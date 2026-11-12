#!/usr/bin/env bash
# ============================================================================
# vmrun.sh -- Sync the project to the guest, build it, and run the tests.
#
# Usage: vmrun.sh [build|test|asan|vg|check|clean|shell]
#
#   build   compile the project in the guest
#   test    run the whole test suite (unit tests + GUI tests)
#   asan    build and run with AddressSanitizer + UndefinedBehaviorSanitizer
#   vg      run the test suite under Valgrind
#   check   build + test (the default)
#   clean   remove the build artefacts inside the guest
#   shell   open an interactive bash inside the guest
#
# Configuration (vmrun.exe path, .vmx path, credentials) lives in
# vm_config.sh and can be overridden through the environment.
# ============================================================================
set -uo pipefail

export MSYS2_ARG_CONV_EXCL='*'
export MSYS_NO_PATHCONV=1

# shellcheck source=vm_config.sh
HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/vm_config.sh"

MODE="${1:-check}"

# ---------------------------------------------------------------------------
# Sync
# ---------------------------------------------------------------------------
"$HERE/vmsync.sh" > /tmp/vmsync.log 2>&1
if [ $? -ne 0 ]; then
    echo "### SYNC FAILED"
    cat /tmp/vmsync.log
    exit 1
fi
echo "### sync ok"

# ---------------------------------------------------------------------------
# Build the guest-side script for the requested mode.
# ---------------------------------------------------------------------------
SCRIPT="$(mktemp)"
trap 'rm -f "$SCRIPT"' EXIT

PROJ="$GUEST_PROJECT"

case "$MODE" in
  build)
    cat > "$SCRIPT" <<EOF
#!/bin/bash
cd "$PROJ" || exit 1
make clean 2>&1
make all 2>&1
echo "### MAKE EXIT: \$?"
EOF
    ;;
  test)
    cat > "$SCRIPT" <<EOF
#!/bin/bash
cd "$PROJ" || exit 1
make test 2>&1
make test-gui 2>&1
echo "### TEST EXIT: \$?"
EOF
    ;;
  asan)
    cat > "$SCRIPT" <<EOF
#!/bin/bash
cd "$PROJ" || exit 1
make asan 2>&1
echo "### ASAN EXIT: \$?"
EOF
    ;;
  vg)
    cat > "$SCRIPT" <<EOF
#!/bin/bash
cd "$PROJ" || exit 1
if ! command -v valgrind >/dev/null 2>&1; then
    echo "### VALGRIND NOT INSTALLED"
    exit 2
fi
make valgrind 2>&1
echo "### VALGRIND EXIT: \$?"
EOF
    ;;
  clean)
    cat > "$SCRIPT" <<EOF
#!/bin/bash
cd "$PROJ" || exit 1
make clean 2>&1
echo "### CLEANED"
EOF
    ;;
  shell)
    "$VMRUN" -T ws -gu "$GUEST_USER" -gp "$GUEST_PASS" \
        runProgramInGuest "$VMX" /bin/bash -c "cd '$PROJ' && exec /bin/bash"
    exit $?
    ;;
  check|*)
    cat > "$SCRIPT" <<EOF
#!/bin/bash
cd "$PROJ" || exit 1
echo "===== BUILD ====="
make clean 2>&1
make all 2>&1
echo "### MAKE EXIT: \$?"
echo ""
echo "===== TESTS ====="
make test 2>&1
make test-gui 2>&1
echo "### TEST EXIT: \$?"
EOF
    ;;
esac

"$HERE/vmbash.sh" "$SCRIPT"
