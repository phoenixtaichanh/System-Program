/* ============================================================================
 * format_utils.c -- Human-readable formatting helpers.
 *
 * Implementation of:
 *   - fmt_size_human   (1024-based: 1.4 KB, 12 MB, ...)
 *   - fmt_time_local   (strftime with a default format)
 *   - fmt_perm_symbolic (e.g. "-rwxr-x---")
 *   - fmt_perm_octal   (e.g. "0750")
 *   - fmt_status_message
 *   - fmt_type_label
 * ============================================================================
 */

#define _XOPEN_SOURCE 700   /* expose S_IF* and S_ISVTX macros */

#include "format_utils.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>

/* ------------------------------------------------------------------ *
 * fmt_size_human
 *   Format `bytes` using 1024-based units up to TiB. Always uses one
 *   decimal place below 10 of the unit, and zero decimal places above.
 *   Examples:
 *     0       -> "0 B"
 *     512     -> "512 B"
 *     1536    -> "1.5 KB"
 *     1048576 -> "1.0 MB"
 *     2621440 -> "2.5 MB"
 * ------------------------------------------------------------------ */
bool
fmt_size_human(unsigned long long bytes, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return false;
    }

    static const char *units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    const int units_n = (int)(sizeof(units) / sizeof(units[0]));

    double size = (double)bytes;
    int u = 0;
    while (size >= 1024.0 && u < units_n - 1) {
        size /= 1024.0;
        u++;
    }

    if (u == 0) {
        /* Whole bytes - no fractional part. */
        return snprintf(out, out_size, "%llu B",
                        (unsigned long long)size) > 0;
    } else if (size < 10.0) {
        return snprintf(out, out_size, "%.1f %s", size, units[u]) > 0;
    } else if (size < 100.0) {
        return snprintf(out, out_size, "%.2f %s", size, units[u]) > 0;
    } else {
        return snprintf(out, out_size, "%.0f %s", size, units[u]) > 0;
    }
}

/* ------------------------------------------------------------------ *
 * fmt_time_local
 *   Convert `t` to a local-time string in the form
 *   "YYYY-MM-DD HH:MM:SS". Returns "(invalid)" for t == (time_t)-1.
 * ------------------------------------------------------------------ */
bool
fmt_time_local(time_t t, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return false;
    }

    if (t == (time_t)-1) {
        snprintf(out, out_size, "(invalid)");
        return true;
    }

    struct tm tm_buf;
    if (localtime_r(&t, &tm_buf) == NULL) {
        snprintf(out, out_size, "(localtime failed)");
        return false;
    }

    /* strftime returns 0 if the result did not fit; we still leave a
     * NUL terminator so the caller has a valid string. */
    size_t n = strftime(out, out_size, "%Y-%m-%d %H:%M:%S", &tm_buf);
    if (n == 0) {
        snprintf(out, out_size, "(strftime overflow)");
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ *
 * fmt_perm_symbolic
 *   Produce a 10-character string like "-rwxr-x---".
 * ------------------------------------------------------------------ */
bool
fmt_perm_symbolic(mode_t mode, char *out, size_t out_size)
{
    if (out == NULL || out_size < 11) {
        return false;
    }

    /* File type character. */
    char t;
    if      (S_ISREG(mode))     t = '-';
    else if (S_ISDIR(mode))     t = 'd';
    else if (S_ISLNK(mode))     t = 'l';
    else if (S_ISCHR(mode))     t = 'c';
    else if (S_ISBLK(mode))     t = 'b';
    else if (S_ISFIFO(mode))    t = 'p';
    else if (S_ISSOCK(mode))    t = 's';
    else                        t = '?';

    /* Permission triplets: rwx for user, group, other. */
    static const char triples[8] = "rwx";
    char p[10];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            p[i * 3 + j] = (mode & (1 << (8 - i * 3 - j))) ? triples[j] : '-';
        }
    }
    /* Set-uid / set-gid / sticky. */
    if (mode & S_ISUID) p[2] = (p[2] == 'x') ? 's' : 'S';
    if (mode & S_ISGID) p[5] = (p[5] == 'x') ? 's' : 'S';
    if (mode & 01000)   p[8] = (p[8] == 'x') ? 't' : 'T';

    snprintf(out, out_size, "%c%s", t, p);
    return true;
}

/* ------------------------------------------------------------------ *
 * fmt_perm_octal
 *   Produce a 4-digit octal string like "0750" (always 4 digits).
 * ------------------------------------------------------------------ */
bool
fmt_perm_octal(mode_t mode, char *out, size_t out_size)
{
    if (out == NULL || out_size < 5) {
        return false;
    }
    snprintf(out, out_size, "%04o", (unsigned int)(mode & 07777));
    return true;
}

/* ------------------------------------------------------------------ *
 * fmt_status_message
 *   Human-readable explanation of an FSStatus + errno. errno is only
 *   consulted for IO/PERM errors.
 * ------------------------------------------------------------------ */
const char *
fmt_status_message(FSStatus s, int errno_code)
{
    switch (s) {
    case FS_OK:
        return "OK";
    case FS_ERR_NOT_FOUND:
        return "Path not found";
    case FS_ERR_PERMISSION:
        return errno_code == EACCES ? "Permission denied"
             : errno_code == EPERM  ? "Operation not permitted"
             : "Access denied";
    case FS_ERR_NOT_DIR:
        return "Not a directory";
    case FS_ERR_IO:
        return strerror(errno_code != 0 ? errno_code : EIO);
    case FS_ERR_INVALID_PATH:
        return "Invalid path";
    case FS_ERR_EXISTS:
        return "Already exists";
    case FS_ERR_BROKEN_LINK:
        return "Broken symbolic link";
    case FS_ERR_NOMEM:
        return "Out of memory";
    case FS_ERR_UNKNOWN:
    default:
        return errno_code != 0 ? strerror(errno_code) : "Unknown error";
    }
}

/* ------------------------------------------------------------------ *
 * fmt_type_label
 *   Short English label for the file type (e.g. "Directory").
 * ------------------------------------------------------------------ */
const char *
fmt_type_label(FSFileType t)
{
    switch (t) {
    case FS_TYPE_REGULAR:    return "Regular file";
    case FS_TYPE_DIRECTORY:  return "Directory";
    case FS_TYPE_SYMLINK:    return "Symbolic link";
    case FS_TYPE_CHAR_DEV:   return "Character device";
    case FS_TYPE_BLOCK_DEV:  return "Block device";
    case FS_TYPE_FIFO:       return "Named pipe (FIFO)";
    case FS_TYPE_SOCKET:     return "Socket";
    case FS_TYPE_UNKNOWN:
    default:                 return "Unknown";
    }
}