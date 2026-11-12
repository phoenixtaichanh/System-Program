#!/bin/bash
# ============================================================================
# vm_install_xdotool.sh -- Install xdotool inside the guest.
#
# Run inside the guest:
#     vmbash.sh vm_install_xdotool.sh
#
# The sudo password can be supplied through SUDO_PASS; otherwise the script
# tries sudo without a password first.
# ============================================================================
set -uo pipefail

if ! command -v xdotool >/dev/null 2>&1; then
    if [ -n "${SUDO_PASS:-}" ]; then
        printf '%s\n' "$SUDO_PASS" | sudo -S apt-get install -y xdotool 2>&1 | tail -5
    else
        sudo -n apt-get install -y xdotool 2>&1 | tail -5 || \
            apt-get install -y xdotool 2>&1 | tail -5
    fi
fi

if command -v xdotool >/dev/null 2>&1; then
    echo "### XDOTOOL OK"
else
    echo "### XDOTOOL MISSING"
    exit 1
fi
