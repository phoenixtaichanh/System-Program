#!/usr/bin/env bash
# ============================================================================
# vmfetch.sh -- Fetch a file from the VMware guest to the host.
#
# Usage: vmfetch.sh <guest-path> <host-path>
# ============================================================================
export MSYS2_ARG_CONV_EXCL='*'
export MSYS_NO_PATHCONV=1

# shellcheck source=vm_config.sh
HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/vm_config.sh"

GUEST_PATH="$1"
HOST_PATH="$2"

"$VMRUN" -T ws -gu "$GUEST_USER" -gp "$GUEST_PASS" \
    copyFileFromGuestToHost "$VMX" "$GUEST_PATH" "$(cygpath -w "$HOST_PATH")"
echo "### fetched $GUEST_PATH -> $HOST_PATH"
