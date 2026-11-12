/* ============================================================================
 * fs_stat_util.h -- Shared helpers for turning a platform stat result into
 * the application's `FileMetadata` structure.
 *
 * `fs_list_dir()`, `fs_get_metadata()` and `fs_search()` all need to
 * populate the same set of fields. Keeping the logic in one place keeps
 * the three services consistent (identical owner/group/permission output)
 * and avoids triplicated code.
 * ============================================================================
 */

#ifndef FS_STAT_UTIL_H
#define FS_STAT_UTIL_H

#include "fs_types.h"

/* Fill `out` from a raw platform stat result.
 * `full_path` is the path of the entry (used for the symlink target
 * lookup when `resolve_target` is true). */
void fs_stat_util_fill(const FsRawStat *st,
                       const char *full_path,
                       int resolve_target,
                       FileMetadata *out);

/* Fill `out` by stat'ing `full_path` itself.
 * Returns FS_OK, or the appropriate error status (errno is preserved in
 * out->errno_code). */
FSStatus fs_stat_util_fill_path(const char *full_path,
                                 const char *display_name,
                                 int resolve_target,
                                 FileMetadata *out);

/* Map a POSIX errno value onto an FSStatus. */
FSStatus fs_stat_util_status_from_errno(int e);

#endif /* FS_STAT_UTIL_H */
