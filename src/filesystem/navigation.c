/* ============================================================================
 * navigation.c -- Working directory helpers.
 *
 * Implements:
 *   - fs_get_cwd  : malloc'd string with current working directory.
 *   - fs_set_cwd  : chdir() with FSStatus mapping.
 *
 * fs_get_cwd() returns malloc'd memory; the caller must free() it.
 * ============================================================================
 */

#define _XOPEN_SOURCE 700

#include "fs_navigation.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Reasonable upper bound for getcwd buffer. PATH_MAX may be huge on some
 * systems; we loop with a growing buffer to be safe. */
#define CWD_INITIAL 1024

char *
fs_get_cwd(void)
{
    size_t cap = CWD_INITIAL;
    char *buf = (char *)malloc(cap);
    if (buf == NULL) return NULL;

    while (getcwd(buf, cap) == NULL) {
        if (errno == ERANGE) {
            size_t new_cap = cap * 2;
            char *nb = (char *)realloc(buf, new_cap);
            if (nb == NULL) {
                free(buf);
                return NULL;
            }
            buf = nb;
            cap = new_cap;
        } else {
            free(buf);
            return NULL;
        }
    }
    return buf;
}

FSStatus
fs_set_cwd(const char *path)
{
    if (path == NULL) return FS_ERR_INVALID_PATH;
    if (path[0] == '\0') return FS_ERR_INVALID_PATH;

    if (chdir(path) == 0) return FS_OK;

    int e = errno;
    switch (e) {
        case ENOENT:        return FS_ERR_NOT_FOUND;
        case ENOTDIR:       return FS_ERR_NOT_DIR;
        case EACCES:
        case EPERM:         return FS_ERR_PERMISSION;
        case ELOOP:
        case ENAMETOOLONG:  return FS_ERR_INVALID_PATH;
        default:            return FS_ERR_IO;
    }
}