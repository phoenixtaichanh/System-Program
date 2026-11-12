/* ============================================================================
 * filesystem_info.c -- File system capacity reporting.
 *
 * Implements:
 *   - fs_get_filesystem_info : statvfs() wrapper that fills an FSInfo
 *                              structure with block/inode counts and a
 *                              human-friendly usage percentage.
 * ============================================================================
 */

#include "fs_filesystem_info.h"
#include "fs_stat_util.h"
#include "path_utils.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

FSStatus
fs_get_filesystem_info(const char *path, FSInfo *out)
{
    if (path == NULL || out == NULL) {
        return FS_ERR_INVALID_PATH;
    }
    if (path[0] == '\0') {
        return FS_ERR_INVALID_PATH;
    }

    memset(out, 0, sizeof(*out));
    snprintf(out->mount_path, FS_PATH_MAX, "%s", path);

    FsRawVfs vfs;
    if (fs_plat_statvfs(path, &vfs) != 0) {
        return fs_stat_util_status_from_errno(errno);
    }

    out->block_size       = vfs.block_size;
    out->total_blocks     = vfs.total_blocks;
    out->free_blocks      = vfs.free_blocks;
    out->available_blocks = vfs.available_blocks;
    out->total_inodes     = vfs.total_inodes;
    out->free_inodes      = vfs.free_inodes;

    /* Byte totals. */
    out->total_bytes = vfs.total_blocks * vfs.block_size;
    out->free_bytes  = vfs.free_blocks  * vfs.block_size;
    out->available_bytes = vfs.available_blocks * vfs.block_size;

    /* Usage percentage, measured against what an unprivileged user can
     * actually use (f_bavail). Guard against a zero denominator. */
    if (vfs.total_blocks > 0 && vfs.block_size > 0) {
        unsigned long long used = 0;
        if (vfs.total_blocks > vfs.available_blocks) {
            used = vfs.total_blocks - vfs.available_blocks;
        }
        double ratio = (double)used / (double)vfs.total_blocks;
        out->usage_percent = ratio * 100.0;
        if (out->usage_percent < 0.0)  out->usage_percent = 0.0;
        if (out->usage_percent > 100.0) out->usage_percent = 100.0;
    } else {
        out->usage_percent = 0.0;
    }

    return FS_OK;
}
