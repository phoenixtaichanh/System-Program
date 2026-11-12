/* ============================================================================
 * path_utils.h -- Path normalization and composition helpers.
 * Implementation lands in PHASE 2.
 * ============================================================================
 */

#ifndef PATH_UTILS_H
#define PATH_UTILS_H

#include "fs_types.h"

#include <stdbool.h>
#include <stddef.h>

/* Returns true if `path` is an absolute path (starts with '/'). */
bool path_is_absolute(const char *path);

/* Returns the home directory of the current user (uses getenv("HOME"),
 * falls back to getpwuid(getuid())->pw_dir). Result is a static buffer. */
const char *path_home_dir(void);

/* Join two path components with a single '/' between them.
 * Writes into `out` (size `out_size`). Returns true on success. */
bool path_join(const char *base, const char *leaf, char *out, size_t out_size);

/* Returns the trailing name component of `path` (e.g. "/a/b/c" -> "c").
 * Result is a static buffer. */
const char *path_basename(const char *path);

/* Returns the directory portion of `path` (everything before the last '/').
 * Writes into `out` (size `out_size`). Returns true on success. */
bool path_dirname(const char *path, char *out, size_t out_size);

/* Resolves `.` and `..` segments in-place inside `path` (no symlink resolution).
 * Returns true on success; false on `EINVAL` (path too long). */
bool path_normalize(char *path, size_t path_size);

/* Resolves a possibly-relative `path` against a base directory using
 * realpath(). Writes the canonical absolute path into `out`. */
bool path_resolve(const char *base, const char *path,
                  char *out, size_t out_size);

#endif /* PATH_UTILS_H */