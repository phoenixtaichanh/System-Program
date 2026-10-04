#!/usr/bin/env bash
# ============================================================================
# make_test_tree.sh -- Create a safe test directory under the user's home.
#
# This script creates ~/fs_explorer_test with:
#   - Regular files (empty, small text, large-ish)
#   - Subdirectories
#   - Working symbolic links
#   - A broken symbolic link
#   - Files with various permissions (readonly, executable)
#
# It uses only simple POSIX commands (ln, touch, chmod, head). It does NOT
# use the application's engine. It is a SETUP tool, not part of the app.
# ============================================================================

set -euo pipefail

BASE="${HOME}/fs_explorer_test"

echo "Creating test tree at ${BASE} ..."

rm -rf "${BASE}"
mkdir -p "${BASE}/Documents"
mkdir -p "${BASE}/Downloads"
mkdir -p "${BASE}/Projects"
mkdir -p "${BASE}/Links"
mkdir -p "${BASE}/Pictures"

# Regular files
: > "${BASE}/empty.txt"
echo "This is a small text file used for the File System Explorer project." \
    > "${BASE}/Documents/report.txt"
echo "Final report content. $(date)" \
    >> "${BASE}/Documents/report.txt"

# A larger text file (~ 1 MB)
yes "Line of text for testing purposes." 2>/dev/null \
    | head -n 30000 > "${BASE}/Downloads/large.txt" \
    || true

# Files in Projects
echo "int main(void) { return 0; }" > "${BASE}/Projects/hello.c"
echo "Project notes."                   > "${BASE}/Projects/notes.txt"

# Permission variations
chmod 644 "${BASE}/Documents/report.txt"
chmod 755 "${BASE}/Projects/hello.c"
chmod 600 "${BASE}/Projects/notes.txt"   # owner-readonly

# Symbolic links
ln -s "${BASE}/Documents/report.txt" "${BASE}/Links/report_link.txt"
ln -s "${BASE}/../nonexistent_file"  "${BASE}/Links/broken_link.txt"
ln -s "${BASE}/Projects"             "${BASE}/Links/projects_link"

echo "Done."
echo
echo "Test tree:"
ls -la "${BASE}"
echo
echo "Broken symlink test:"
ls -la "${BASE}/Links/broken_link.txt" || true