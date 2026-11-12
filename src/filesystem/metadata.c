/* ============================================================================
 * metadata.c -- Detailed metadata fetcher.
 *
 * Implements:
 *   - fs_get_metadata : lstat() a single path and fill a FileMetadata.
 *
 * The symlink target is *not* followed: a symbolic link is reported as a
 * symbolic link, with its raw target string and (optionally) whether that
 * target exists.
 * ============================================================================
 */

#include "fs_metadata.h"
#include "fs_stat_util.h"
#include "format_utils.h"
#include "path_utils.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

FSStatus
fs_get_metadata(const char *path, int resolve_target, FileMetadata *out)
{
    if (path == NULL || out == NULL) {
        return FS_ERR_INVALID_PATH;
    }
    if (path[0] == '\0') {
        return FS_ERR_INVALID_PATH;
    }

    return fs_stat_util_fill_path(path, NULL, resolve_target, out);
}
