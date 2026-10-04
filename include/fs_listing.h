/* ============================================================================
 * fs_listing.h -- Directory listing service.
 * Implementation lands in PHASE 3.
 * ============================================================================
 */

#ifndef FS_LISTING_H
#define FS_LISTING_H

#include "fs_types.h"

/* List `path` into `out_entries` (caller-allocated array of size `*count_io`).
 * On input, `*count_io` is the maximum number of entries the array can hold.
 * On output, `*count_io` is the actual number of entries written.
 *
 * Entries that fail to stat are still included (with status set), so the GUI
 * can show "Permission denied" rows gracefully. */
FSStatus fs_list_dir(const char *path,
                     DirEntry *out_entries,
                     int *count_io);

#endif /* FS_LISTING_H */