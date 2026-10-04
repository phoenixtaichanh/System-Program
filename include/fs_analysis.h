/* ============================================================================
 * fs_analysis.h -- Recursive directory analyzer.
 * Implementation lands in PHASE 9.
 * ============================================================================
 */

#ifndef FS_ANALYSIS_H
#define FS_ANALYSIS_H

#include "fs_types.h"

/* Walk `path` recursively, accumulating counts, total size, top-N largest
 * files, and per-subdirectory totals into `out_stats`. Symbolic links are NOT
 * followed. Returns FS_OK if the traversal completed, even if some sub-trees
 * were skipped due to permission errors. */
FSStatus fs_analyze_directory(const char *path, DirStats *out_stats);

#endif /* FS_ANALYSIS_H */