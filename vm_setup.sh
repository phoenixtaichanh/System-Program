#!/bin/bash
# ============================================================================
# vm_setup.sh -- Prepare a fresh Ubuntu guest for building and testing the
#                File System Explorer.
#
# Run inside the guest (for example through vmbash.sh):
#
#     vmbash.sh vm_setup.sh
#
# The script installs everything the project needs:
#   - a C11 toolchain and GNU make
#   - GTK 3 and GLib development files (pkg-config)
#   - Valgrind for the memory-safety pass
#   - Xvfb, ImageMagick and xdotool for the headless GUI tests
#
# It is idempotent: running it twice is harmless.
# ============================================================================
set -uo pipefail

echo "=========================================================="
echo " File System Explorer - guest setup"
echo "=========================================================="
echo ""
echo "--- distribution ---"
. /etc/os-release 2>/dev/null && echo "$PRETTY_NAME" || cat /etc/lsb-release 2>/dev/null
echo "--- kernel ---"
uname -a
echo ""

echo "=== 1/4  update the package lists ==="
sudo -n apt-get update -qq 2>&1 | tail -3 || \
    apt-get update -qq 2>&1 | tail -3 || echo "!! apt-get update failed"

echo ""
echo "=== 2/4  install the build toolchain and GTK 3 ==="
sudo -n apt-get install -y -qq \
    build-essential pkg-config libgtk-3-dev \
    2>&1 | tail -5 || \
    apt-get install -y -qq build-essential pkg-config libgtk-3-dev 2>&1 | tail -5

echo ""
echo "=== 3/4  install Valgrind (memory-safety pass) ==="
sudo -n apt-get install -y -qq valgrind 2>&1 | tail -3 || \
    apt-get install -y -qq valgrind 2>&1 | tail -3 || echo "!! valgrind not available"

echo ""
echo "=== 4/4  install the headless GUI test tools ==="
sudo -n apt-get install -y -qq \
    xvfb x11-utils imagemagick xdotool 2>&1 | tail -3 || \
    apt-get install -y -qq xvfb x11-utils imagemagick xdotool 2>&1 | tail -3

echo ""
echo "=========================================================="
echo " Verification"
echo "=========================================================="

printf '  gcc          : '; gcc --version 2>/dev/null | head -1 || echo "MISSING"
printf '  make         : '; make --version 2>/dev/null | head -1 || echo "MISSING"
printf '  pkg-config   : '; pkg-config --version 2>/dev/null || echo "MISSING"
printf '  gtk+-3.0     : '; pkg-config --modversion gtk+-3.0 2>/dev/null || echo "MISSING"
printf '  glib-2.0     : '; pkg-config --modversion glib-2.0 2>/dev/null || echo "MISSING"
printf '  valgrind     : '; valgrind --version 2>/dev/null || echo "MISSING"
printf '  Xvfb         : '; command -v Xvfb || echo "MISSING"
printf '  xdotool      : '; command -v xdotool || echo "MISSING"
printf '  import (IM)  : '; command -v import || echo "MISSING"

echo ""
echo "### SETUP DONE"
