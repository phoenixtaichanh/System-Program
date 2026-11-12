/* ============================================================================
 * fs_operations.h -- Safe file system operations.
 * Implementation lands in PHASE 11.
 * ============================================================================
 */

#ifndef FS_OPERATIONS_H
#define FS_OPERATIONS_H

#include "fs_types.h"

/* Create directory `path` (recursively if `recursive` is true). */
FSStatus fs_mkdir(const char *path, int recursive);

/* Create an empty file at `path` using open(O_CREAT|O_WRONLY|O_EXCL, 0644).
 * Fails if the file already exists. */
FSStatus fs_touch(const char *path);

/* Rename `old_path` to `new_path`. Both must be on the same file system. */
FSStatus fs_rename(const char *old_path, const char *new_path);

/* Remove a regular file (unlink) or empty directory (rmdir) at `path`.
 * Auto-detects the type using lstat. */
FSStatus fs_remove(const char *path);

/* Copy a regular file (or recreate a symbolic link) from `src_path` to
 * `dst_path`. An existing destination file is overwritten. */
FSStatus fs_copy(const char *src_path, const char *dst_path);

#endif /* FS_OPERATIONS_H */