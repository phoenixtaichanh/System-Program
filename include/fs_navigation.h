/* ============================================================================
 * fs_navigation.h -- Working directory helpers.
 * Implementation lands in PHASE 3.
 * ============================================================================
 */

#ifndef FS_NAVIGATION_H
#define FS_NAVIGATION_H

#include "fs_types.h"

/* Return the current working directory in an allocated string. Caller frees. */
char *fs_get_cwd(void);

/* Change the current working directory. */
FSStatus fs_set_cwd(const char *path);

#endif /* FS_NAVIGATION_H */