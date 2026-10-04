/* ============================================================================
 * fs_symlink.h -- Symbolic link helpers.
 * Implementation lands in PHASE 7.
 * ============================================================================
 */

#ifndef FS_SYMLINK_H
#define FS_SYMLINK_H

#include "fs_types.h"

/* Read the raw link target into `out_target`. Returns:
 *   FS_OK            on success
 *   FS_ERR_NOT_FOUND if path is missing
 *   FS_ERR_INVALID_PATH if path is not a symbolic link
 */
FSStatus fs_read_link_target(const char *path,
                             char *out_target,
                             size_t out_target_size);

/* Returns 1 if the target of `link_path` exists, 0 if it does not, -1 on
 * stat() error (e.g. EACCES). The check is done with stat(), which follows
 * the link. */
int fs_link_target_exists(const char *link_path);

#endif /* FS_SYMLINK_H */