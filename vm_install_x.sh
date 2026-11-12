#!/bin/bash
# ============================================================================
# vm_install_x.sh -- Install the packages needed for headless GUI testing.
#
# Run inside the guest:
#     vmbash.sh vm_install_x.sh
#
# The script uses sudo without a password when possible and falls back to a
# plain apt-get call otherwise.
# ============================================================================
set -uo pipefail

echo "=== apt update ==="
sudo -n apt-get update -qq 2>&1 | tail -3 || apt-get update -qq 2>&1 | tail -3

echo "=== install xvfb + imagemagick + xdotool ==="
sudo -n apt-get install -y -qq xvfb x11-utils imagemagick xdotool 2>&1 | tail -5 || \
    apt-get install -y -qq xvfb x11-utils imagemagick xdotool 2>&1 | tail -5

echo "=== verify ==="
which Xvfb xvfb-run import xdotool xwininfo 2>&1
echo "### INSTALL DONE"
