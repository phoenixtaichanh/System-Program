/* ============================================================================
 * symlink.c -- Symbolic link helpers.
 *
 * Implements:
 *   - fs_read_link_target   : raw target string of a symbolic link.
 *   - fs_link_target_exists  : whether the target resolves to an existing
 *                              file (the link is followed for this test).
 *   - fs_create_symlink      : create a symbolic link (extra convenience
 *                              used by the CLI and the test suite).
 *   - fs_is_symlink          : quick type test.
 *
 * All calls are routed through fs_platform.h so the behaviour is the same
 * on Linux and Windows.
 * ============================================================================
 */

#include "fs_symlink.h"
#include "fs_stat_util.h"
#include "path_utils.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ *
 * fs_read_link_target
 *
 * Writes the raw (unresolved) target of the symbolic link at `path` into
 * `out_target`.
 *
 * Returns:
 *   FS_OK            success
 *   FS_ERR_NOT_FOUND the link itself does not exist
 *   FS_ERR_INVALID_PATH the path exists but is not a symbolic link
 *   FS_ERR_IO        any other failure (e.g. permission denied)
 * ------------------------------------------------------------------ */
FSStatus
fs_read_link_target(const char *path, char *out_target, size_t out_target_size)
{
    if (path == NULL || out_target == NULL || out_target_size == 0) {
        return FS_ERR_INVALID_PATH;
    }
    if (path[0] == '\0') {
        return FS_ERR_INVALID_PATH;
    }

    out_target[0] = '\0';

    /* First check that the path exists and really is a link, so we can
     * report a precise error instead of a generic I/O failure. */
    FsRawStat st;
    if (fs_plat_lstat(path, &st) != 0) {
        return fs_stat_util_status_from_errno(errno);
    }
    if (!FS_S_ISLNK(st.mode) && !st.is_symlink) {
        return FS_ERR_INVALID_PATH;
    }

    if (fs_plat_readlink(path, out_target, out_target_size) != 0) {
        return fs_stat_util_status_from_errno(errno);
    }

    return FS_OK;
}

/* ------------------------------------------------------------------ *
 * fs_link_target_exists
 *
 * Returns 1  if the link target exists,
 *         -1  if it does not exist (broken link),
 *          0  on a stat() error other than "not found" (e.g. EACCES).
 *
 * This matches the convention used by FileMetadata.link_target_exists.
 * ------------------------------------------------------------------ */
int
fs_link_target_exists(const char *link_path)
{
    if (link_path == NULL || link_path[0] == '\0') {
        return 0;
    }

    FsRawStat st;
    if (fs_plat_stat(link_path, &st) == 0) {
        return 1;
    }

    int e = errno;
    if (e == ENOENT || e == ENOTDIR) {
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ *
 * fs_is_symlink
 * ------------------------------------------------------------------ */
int
fs_is_symlink(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return 0;
    }

    FsRawStat st;
    if (fs_plat_lstat(path, &st) != 0) {
        return 0;
    }
    return (FS_S_ISLNK(st.mode) || st.is_symlink) ? 1 : 0;
}

/* ------------------------------------------------------------------ *
 * fs_create_symlink
 *
 * Creates a symbolic link at `link_path` pointing at `target`.
 * `target` may be relative (it is then interpreted relative to the
 * directory containing `link_path`, exactly like `ln -s`).
 * ------------------------------------------------------------------ */
FSStatus
fs_create_symlink(const char *target, const char *link_path)
{
    if (target == NULL || link_path == NULL ||
        target[0] == '\0' || link_path[0] == '\0') {
        return FS_ERR_INVALID_PATH;
    }

    if (fs_plat_symlink(target, link_path) != 0) {
        return fs_stat_util_status_from_errno(errno);
    }
    return FS_OK;
}
