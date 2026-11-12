/* ============================================================================
 * format_utils.h -- Human-readable formatting helpers.
 * Implementation lands in PHASE 2.
 * ============================================================================
 */

#ifndef FORMAT_UTILS_H
#define FORMAT_UTILS_H

#include "fs_types.h"

#include <stddef.h>
#include <stdbool.h>
#include <time.h>

/* Format `bytes` as a human-readable size (e.g. "1.4 KB", "12 MB").
 * Writes into `out` (size `out_size`). Returns true on success. */
bool fmt_size_human(unsigned long long bytes, char *out, size_t out_size);

/* Format `t` using localtime_r + strftime into `out`. Format example:
 *   "2026-10-04 15:42:18".
 * Returns true on success. */
bool fmt_time_local(time_t t, char *out, size_t out_size);

/* Convert mode bits to symbolic form (e.g. "-rwxr-x---"). */
bool fmt_perm_symbolic(unsigned int mode, char *out, size_t out_size);

/* Convert mode bits to octal string (e.g. "0750"). */
bool fmt_perm_octal(unsigned int mode, char *out, size_t out_size);

/* Translate FSStatus + errno to a user-friendly message. */
const char *fmt_status_message(FSStatus s, int errno_code);

/* Translate FSFileType to a short display string (e.g. "Directory"). */
const char *fmt_type_label(FSFileType t);

#endif /* FORMAT_UTILS_H */