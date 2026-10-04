/* ============================================================================
 * fs_filesystem_info.h -- statvfs wrapper.
 * Implementation lands in PHASE 10.
 * ============================================================================
 */

#ifndef FS_FILESYSTEM_INFO_H
#define FS_FILESYSTEM_INFO_H

#include "fs_types.h"

typedef struct {
    char                mount_path[FS_PATH_MAX];

    unsigned long long  block_size;
    unsigned long long  total_blocks;
    unsigned long long  free_blocks;       /* for superuser */
    unsigned long long  available_blocks;  /* for unprivileged user */
    unsigned long long  total_inodes;
    unsigned long long  free_inodes;

    unsigned long long  total_bytes;
    unsigned long long  free_bytes;        /* for superuser */
    unsigned long long  available_bytes;   /* for unprivileged user */

    double              usage_percent;
} FSInfo;

FSStatus fs_get_filesystem_info(const char *path, FSInfo *out);

#endif /* FS_FILESYSTEM_INFO_H */