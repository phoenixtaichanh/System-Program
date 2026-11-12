#!/usr/bin/env bash
# ============================================================================
# vmexec.sh -- Run a command inside the VMware Ubuntu guest and return stdout.
#
# Usage: vmexec.sh "<command string>"
#
# Mechanism (no SSH required):
#   1. Copy a runner script into the guest via vmrun copyFileFromHostToGuest.
#   2. The runner writes the command output to a result file.
#   3. Copy the result file back to the host.
#
# Notes:
#   - vmrun's runProgramInGuest does not capture stdout, so we redirect to a
#     file inside the guest and fetch that file.
#   - The command is passed base64-encoded to avoid all quoting issues.
#   - Every run uses unique file names so that repeated or parallel runs cannot
#     overwrite each other.
#   - A short settle delay plus a retry loop is used before fetching, because
#     vmrun can return before the guest has flushed the output file.
# ============================================================================
set -uo pipefail

export MSYS2_ARG_CONV_EXCL='*'
export MSYS_NO_PATHCONV=1

# shellcheck source=vm_config.sh
HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/vm_config.sh"

TMPDIR_HOST="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_HOST"' EXIT

CMD="$1"

# A unique tag so that repeated or parallel runs never collide.
TAG="vme_$$_$(date +%s)"

GUEST_RUNNER="$GUEST_HOME/.$TAG.runner"
GUEST_INNER="$GUEST_HOME/.$TAG.inner"
GUEST_OUT="$GUEST_HOME/.$TAG.out"

# ---- Build the runner script inside the guest --------------------------------
RUNNER="$TMPDIR_HOST/runner.sh"
{
  echo '#!/bin/bash'
  echo 'OUT="$2"'
  echo '{'
  echo '  echo "### CMD: $1"'
  echo '  bash -c "$1" 2>&1'
  echo '  echo "### EXIT: $?"'
  echo '} > "$OUT" 2>&1'
} > "$RUNNER"

# Encode the command as base64 so no quoting can mangle it.
CMD_B64="$(printf '%s' "$CMD" | base64 -w0)"

INNER="$TMPDIR_HOST/inner.sh"
{
  echo '#!/bin/bash'
  echo "DEC=\$(printf '%s' '$CMD_B64' | base64 -d)"
  echo "bash $GUEST_RUNNER \"\$DEC\" $GUEST_OUT"
  echo "sync"
} > "$INNER"

# ---- Copy files into the guest ----------------------------------------------
# vmrun is a native Windows program, so the host paths must be converted.
RUNNER_WIN="$(cygpath -w "$RUNNER")"
INNER_WIN="$(cygpath -w "$INNER")"

"$VMRUN" -T ws -gu "$GUEST_USER" -gp "$GUEST_PASS" \
    copyFileFromHostToGuest "$VMX" "$RUNNER_WIN" "$GUEST_RUNNER" || exit 1
"$VMRUN" -T ws -gu "$GUEST_USER" -gp "$GUEST_PASS" \
    copyFileFromHostToGuest "$VMX" "$INNER_WIN" "$GUEST_INNER" || exit 1

# ---- Execute ----------------------------------------------------------------
"$VMRUN" -T ws -gu "$GUEST_USER" -gp "$GUEST_PASS" \
    runProgramInGuest "$VMX" /bin/bash "$GUEST_INNER" || exit 1

# Give the guest a moment to flush the output file, then poll for it.
sleep 2
OUT_WIN="$(cygpath -w "$TMPDIR_HOST/vme_out")"
for _ in 1 2 3 4 5 6 7 8 9 10; do
    "$VMRUN" -T ws -gu "$GUEST_USER" -gp "$GUEST_PASS" \
        copyFileFromGuestToHost "$VMX" "$GUEST_OUT" "$OUT_WIN" >/dev/null 2>&1
    [ -s "$TMPDIR_HOST/vme_out" ] && break
    sleep 1
done

# ---- Clean up the staged files in the guest ---------------------------------
{
  echo '#!/bin/bash'
  echo "rm -f '$GUEST_RUNNER' '$GUEST_INNER' '$GUEST_OUT'"
} > "$TMPDIR_HOST/cleanup.sh"
CLEANUP_WIN="$(cygpath -w "$TMPDIR_HOST/cleanup.sh")"
"$VMRUN" -T ws -gu "$GUEST_USER" -gp "$GUEST_PASS" \
    copyFileFromHostToGuest "$VMX" "$CLEANUP_WIN" "$GUEST_HOME/.$TAG.cleanup" >/dev/null 2>&1
"$VMRUN" -T ws -gu "$GUEST_USER" -gp "$GUEST_PASS" \
    runProgramInGuest "$VMX" /bin/bash "$GUEST_HOME/.$TAG.cleanup" >/dev/null 2>&1

# ---- Report -----------------------------------------------------------------
if [ -s "$TMPDIR_HOST/vme_out" ]; then
    cat "$TMPDIR_HOST/vme_out"
else
    echo "### VMEXEC: no output captured"
    exit 1
fi
