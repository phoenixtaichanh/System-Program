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

/* Returns 1 if the target of `link_path` exists, -1 if it does not
 * (broken link), and 0 on a stat() error (e.g. EACCES) or when the path
 * is not a link at all. The check is done with stat(), which follows the
 * link. */
int fs_link_target_exists(const char *link_path);

/* Returns 1 if `path` is a symbolic link, 0 otherwise (including when the
 * path does not exist). */
int fs_is_symlink(const char *path);

/* Create a symbolic link at `link_path` whose target is `target`.
 * `target` may be relative; it is then interpreted relative to the
 * directory containing `link_path`. */
FSStatus fs_create_symlink(const char *target, const char *link_path);

#endif /* FS_SYMLINK_H */