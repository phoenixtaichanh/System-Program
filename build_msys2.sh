#!/usr/bin/env bash
# ============================================================================
# build_msys2.sh -- Build and test the project on MSYS2 (Windows / MinGW-w64).
#
# Verifies that the secondary target platform still compiles cleanly and that
# the whole test suite passes there.
#
# Run it from the project directory:
#     ./build_msys2.sh
# ============================================================================
export PATH="/ucrt64/bin:$PATH"

HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE" || exit 1

echo "=== pkg-config gtk+-3.0 ==="
pkg-config --cflags gtk+-3.0 || exit 1
echo "=== pkg-config glib-2.0 gobject-2.0 ==="
pkg-config --cflags glib-2.0 gobject-2.0 || exit 1

echo ""
echo "=== make clean ==="
make clean 2>&1 | tail -3

echo ""
echo "=== make all ==="
make all 2>&1 | tail -60
echo "### MAKE EXIT: $?"

echo ""
echo "=== make test ==="
make test 2>&1 | tail -40
echo "### TEST EXIT: $?"

ls -la build/*.exe 2>/dev/null
