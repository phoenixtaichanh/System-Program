#!/usr/bin/env bash
# ============================================================================
# vmbash.sh -- Execute a bash script inside the VMware Ubuntu guest.
#
# Usage:
#   vmbash.sh <host-script-file>        # run a script file in the guest
#
# The script's stdout+stderr is captured and printed on the host.
#
# Path handling:
#   - Host-side arguments use Windows paths (vmrun.exe is a native program).
#   - Guest-side paths stay POSIX.
#   - MSYS2's automatic argument/path conversion is disabled so that guest
#     paths such as /home/tuananh are not rewritten.
# ============================================================================
set -uo pipefail

export MSYS2_ARG_CONV_EXCL='*'
export MSYS_NO_PATHCONV=1

# shellcheck source=vm_config.sh
HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/vm_config.sh"

SRC="$1"
if [ ! -f "$SRC" ]; then
    echo "### VMBASH: source not found: $SRC"
    exit 1
fi

TMPDIR_HOST="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_HOST"' EXIT

# Normalise to LF-only and strip a possible BOM (CRLF breaks bash).
tr -d '\r' < "$SRC" | sed '1s/^\xEF\xBB\xBF//' > "$TMPDIR_HOST/script.sh"

# Windows path of the staged script (for copyFileFromHostToGuest).
HOST_SCRIPT_WIN="$(cygpath -w "$TMPDIR_HOST/script.sh")"
HOST_RUNNER_WIN="$(cygpath -w "$TMPDIR_HOST/runner.sh")"
HOST_INNER_WIN="$(cygpath -w "$TMPDIR_HOST/inner.sh")"
HOST_OUT_WIN="$(cygpath -w "$TMPDIR_HOST/vme_out")"

# Wrap: run the script, capture everything into a result file.
{
  echo '#!/bin/bash'
  echo 'OUT="$1"'
  echo '{'
  echo '  echo "### SCRIPT: $2"'
  echo '  bash "$2" 2>&1'
  echo '  echo "### EXIT: $?"'
  echo '} > "$OUT" 2>&1'
} > "$TMPDIR_HOST/runner.sh"

{
  echo '#!/bin/bash'
  echo "bash /home/tuananh/.vme_runner /home/tuananh/.vme_out /home/tuananh/.vme_script.sh"
  echo 'sync'
} > "$TMPDIR_HOST/inner.sh"

"$VMRUN" -T ws -gu "$GUEST_USER" -gp "$GUEST_PASS" \
    copyFileFromHostToGuest "$VMX" "$HOST_RUNNER_WIN"  "$GUEST_HOME/.vme_runner"    >/dev/null 2>&1
"$VMRUN" -T ws -gu "$GUEST_USER" -gp "$GUEST_PASS" \
    copyFileFromHostToGuest "$VMX" "$HOST_INNER_WIN"   "$GUEST_HOME/.vme_inner.sh"  >/dev/null 2>&1
"$VMRUN" -T ws -gu "$GUEST_USER" -gp "$GUEST_PASS" \
    copyFileFromHostToGuest "$VMX" "$HOST_SCRIPT_WIN"  "$GUEST_HOME/.vme_script.sh" >/dev/null 2>&1

"$VMRUN" -T ws -gu "$GUEST_USER" -gp "$GUEST_PASS" \
    runProgramInGuest "$VMX" /bin/bash "$GUEST_HOME/.vme_inner.sh" >/dev/null 2>&1

# Give the guest a moment to flush the output file.
sleep 2

"$VMRUN" -T ws -gu "$GUEST_USER" -gp "$GUEST_PASS" \
    copyFileFromGuestToHost "$VMX" "$GUEST_HOME/.vme_out" "$HOST_OUT_WIN" >/dev/null 2>&1

if [ -s "$TMPDIR_HOST/vme_out" ]; then
    cat "$TMPDIR_HOST/vme_out"
else
    echo "### VMBASH: no output captured"
    exit 1
fi
