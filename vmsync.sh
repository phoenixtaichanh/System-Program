#!/usr/bin/env bash
# ============================================================================
# vmsync.sh -- Copy the project tree from the host into the VMware guest.
#
# Usage: vmsync.sh [source-dir] [guest-dir]
#
# The tree is packaged into a tarball on the host, copied into the guest
# with vmrun, and extracted there. Build artefacts are excluded.
# ============================================================================
set -uo pipefail

export MSYS2_ARG_CONV_EXCL='*'
export MSYS_NO_PATHCONV=1

# shellcheck source=vm_config.sh
HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/vm_config.sh"

SRC="${1:-$HOST_PROJECT}"
DST="${2:-$GUEST_PROJECT}"

echo "### syncing $SRC -> $DST"

# Package the tree (excluding build artifacts) into a tarball on the host.
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

tar -czf "$TMP/src.tar.gz" \
    --exclude='./build' \
    --exclude='./*.o' \
    --exclude='./*.d' \
    --exclude='./.git' \
    -C "$SRC" .

echo "### tarball: $(du -h "$TMP/src.tar.gz" | cut -f1)"

# Copy the tarball into the guest.
TAR_WIN="$(cygpath -w "$TMP/src.tar.gz")"
"$VMRUN" -T ws -gu "$GUEST_USER" -gp "$GUEST_PASS" \
    copyFileFromHostToGuest "$VMX" "$TAR_WIN" "$GUEST_HOME/src.tar.gz" || exit 1

echo "### copied tarball; extracting in guest..."

# Extract in the guest and report the result.
cat > "$TMP/extract.sh" <<EOF
#!/bin/bash
set -uo pipefail
DST="\$1"
rm -rf "\$DST"
mkdir -p "\$DST"
tar -xzf $GUEST_HOME/src.tar.gz -C "\$DST" 2>&1
echo "### extracted to \$DST"
echo "### file count: \$(find "\$DST" -type f | wc -l)"
EOF
tr -d '\r' < "$TMP/extract.sh" > "$TMP/extract_lf.sh"
EXTRACT_WIN="$(cygpath -w "$TMP/extract_lf.sh")"

"$VMRUN" -T ws -gu "$GUEST_USER" -gp "$GUEST_PASS" \
    copyFileFromHostToGuest "$VMX" "$EXTRACT_WIN" "$GUEST_HOME/extract.sh" >/dev/null 2>&1
"$VMRUN" -T ws -gu "$GUEST_USER" -gp "$GUEST_PASS" \
    runProgramInGuest "$VMX" /bin/bash "$GUEST_HOME/extract.sh" "$DST" >/dev/null 2>&1

sleep 2

# Fetch the extraction log.
cat > "$TMP/ls.sh" <<EOF
#!/bin/bash
{ echo "### extracted to $DST"; echo "### file count: \$(find "$DST" -type f | wc -l)"; } > $GUEST_HOME/.sync_out 2>&1
EOF
tr -d '\r' < "$TMP/ls.sh" > "$TMP/ls_lf.sh"
LS_WIN="$(cygpath -w "$TMP/ls_lf.sh")"

"$VMRUN" -T ws -gu "$GUEST_USER" -gp "$GUEST_PASS" \
    copyFileFromHostToGuest "$VMX" "$LS_WIN" "$GUEST_HOME/ls.sh" >/dev/null 2>&1
"$VMRUN" -T ws -gu "$GUEST_USER" -gp "$GUEST_PASS" \
    runProgramInGuest "$VMX" /bin/bash "$GUEST_HOME/ls.sh" >/dev/null 2>&1

sleep 2

OUT_WIN="$(cygpath -w "$TMP/sync_out")"
"$VMRUN" -T ws -gu "$GUEST_USER" -gp "$GUEST_PASS" \
    copyFileFromGuestToHost "$VMX" "$GUEST_HOME/.sync_out" "$OUT_WIN" >/dev/null 2>&1

if [ -s "$TMP/sync_out" ]; then
    cat "$TMP/sync_out"
else
    echo "### VMSYNC: could not verify (extraction may still be running)"
    exit 1
fi
