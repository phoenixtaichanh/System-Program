#!/usr/bin/env bash
# ============================================================================
# vm_ready.sh -- Wait until the Ubuntu guest answers guest operations.
#
# Usage: vm_ready.sh
#
# Run this right after starting the virtual machine; it polls vmrun until the
# guest is fully booted and VMware Tools are running.
# ============================================================================
export MSYS2_ARG_CONV_EXCL='*'
export MSYS_NO_PATHCONV=1

# shellcheck source=vm_config.sh
HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/vm_config.sh"

echo "=== waiting for guest (up to 300s) ==="
for i in $(seq 1 60); do
    if "$VMRUN" -T ws -gu "$GUEST_USER" -gp "$GUEST_PASS" \
            runProgramInGuest "$VMX" /bin/echo ready >/dev/null 2>&1; then
        echo "GUEST READY after $i attempts"
        exit 0
    fi
    sleep 5
done
echo "GUEST NOT READY"
exit 1
