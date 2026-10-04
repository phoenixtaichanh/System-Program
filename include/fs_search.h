/* ============================================================================
 * fs_search.h -- Recursive name search.
 * Implementation lands in PHASE 8.
 * ============================================================================
 */

#ifndef FS_SEARCH_H
#define FS_SEARCH_H

#include "fs_types.h"

/* Result of one match. */
typedef struct {
    FileMetadata meta;
} SearchResult;

/* Recursively search `root` for entries whose basename matches `pattern`.
 * `pattern` may contain '*' and '?' wildcards (handled by fnmatch). If
 * `literal_only` is true, a plain strstr() match is performed instead.
 *
 * Symlinks are not followed. `max_depth` of -1 means unlimited.
 * `max_results` caps the number of matches written into `out_results`.
 *
 * On input, `*result_count_io` is the array capacity.
 * On output, it is the number of matches written.
 * The traversal always runs to completion (or until `max_results` is hit). */
FSStatus fs_search(const char *root,
                   const char *pattern,
                   int literal_only,
                   int max_depth,
                   int max_results,
                   SearchResult *out_results,
                   int *result_count_io);

#endif /* FS_SEARCH_H */