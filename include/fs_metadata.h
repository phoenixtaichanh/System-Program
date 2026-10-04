/* ============================================================================
 * fs_metadata.h -- Detailed metadata fetcher.
 * Implementation lands in PHASE 4.
 * ============================================================================
 */

#ifndef FS_METADATA_H
#define FS_METADATA_H

#include "fs_types.h"

/* Populate `out` with information about `path`. Uses lstat() so symlinks are
 * detected and not followed. If `path` is a symlink and `resolve_target` is
 * true, the target's existence is also reported in link_target_exists. */
FSStatus fs_get_metadata(const char *path,
                         int resolve_target,
                         FileMetadata *out);

#endif /* FS_METADATA_H */