/* ============================================================================
 * operations.c -- Safe file system operations.
 *
 * Implements:
 *   - fs_mkdir   : create a directory, optionally creating missing parents.
 *   - fs_touch  : create an empty file (fails if it already exists).
 *   - fs_rename : rename / move a path.
 *   - fs_remove : remove a regular file or an empty directory.
 *   - fs_copy   : copy a regular file (convenience for the GUI).
 *
 * Safety rules honoured by every operation:
 *   - The parent directory of the target is checked for existence.
 *   - "remove" never deletes recursively; the user must empty a directory
 *     first. This is a deliberate guard against accidental data loss.
 *   - Every errno is mapped onto an FSStatus so the UI can show a
 *     meaningful message.
 * ============================================================================
 */

#include "fs_operations.h"
#include "fs_stat_util.h"
#include "path_utils.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ------------------------------------------------------------------ *
 * Binary file access
 *
 * On Windows the C runtime opens files in text mode by default, which
 * translates every "\n" into "\r\n" and corrupts binary content. The
 * "_O_BINARY" flag turns the translation off; on POSIX the flag does not
 * exist and is not needed.
 * ------------------------------------------------------------------ */
#ifdef _WIN32
#  include <io.h>
#  define FS_O_BINARY _O_BINARY
#else
#  define FS_O_BINARY 0
#endif

/* ------------------------------------------------------------------ *
 * fs_mkdir
 * ------------------------------------------------------------------ */
FSStatus
fs_mkdir(const char *path, int recursive)
{
    if (path == NULL || path[0] == '\0') {
        return FS_ERR_INVALID_PATH;
    }

    if (!recursive) {
        if (fs_plat_mkdir(path, 0755) == 0) {
            return FS_OK;
        }
        return fs_stat_util_status_from_errno(errno);
    }

    /* Recursive mode: walk the path components, creating each one.
     * We work on a mutable copy so we can chop off one segment at a time. */
    char work[FS_PATH_MAX];
    snprintf(work, sizeof(work), "%s", path);

    size_t len = strlen(work);
    /* Strip trailing slashes (but keep the root "/"). */
    while (len > 1 && work[len - 1] == '/') {
        work[--len] = '\0';
    }

    for (size_t i = 1; i <= len; i++) {
        if (work[i] != '/' && work[i] != '\0') {
            continue;
        }

        char saved = work[i];
        work[i] = '\0';

        if (work[0] != '\0') {
            FsRawStat st;
            if (fs_plat_lstat(work, &st) == 0) {
                if (!FS_S_ISDIR(st.mode)) {
                    work[i] = saved;
                    errno = ENOTDIR;
                    return FS_ERR_NOT_DIR;
                }
                work[i] = saved;
                continue;   /* already exists - fine */
            }
            if (errno != ENOENT) {
                int e = errno;
                work[i] = saved;
                errno = e;
                return fs_stat_util_status_from_errno(e);
            }

            if (fs_plat_mkdir(work, 0755) != 0) {
                int e = errno;
                /* Losing a race with another process is not an error. */
                if (e == EEXIST) {
                    work[i] = saved;
                    continue;
                }
                work[i] = saved;
                errno = e;
                return fs_stat_util_status_from_errno(e);
            }
        }

        work[i] = saved;
    }

    return FS_OK;
}

/* ------------------------------------------------------------------ *
 * fs_touch
 * ------------------------------------------------------------------ */
FSStatus
fs_touch(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return FS_ERR_INVALID_PATH;
    }

    /* O_EXCL guarantees we never truncate an existing file. */
    int fd = open(path, O_CREAT | O_WRONLY | O_EXCL, 0644);
    if (fd < 0) {
        return fs_stat_util_status_from_errno(errno);
    }
    close(fd);
    return FS_OK;
}

/* ------------------------------------------------------------------ *
 * fs_rename
 * ------------------------------------------------------------------ */
FSStatus
fs_rename(const char *old_path, const char *new_path)
{
    if (old_path == NULL || new_path == NULL ||
        old_path[0] == '\0' || new_path[0] == '\0') {
        return FS_ERR_INVALID_PATH;
    }

    if (rename(old_path, new_path) == 0) {
        return FS_OK;
    }
    return fs_stat_util_status_from_errno(errno);
}

/* ------------------------------------------------------------------ *
 * fs_remove
 *
 * Removes a regular file, a symbolic link, or an *empty* directory.
 * Directories that still contain entries fail with FS_ERR_IO and the
 * errno ENOTEMPTY, so the UI can explain the problem.
 * ------------------------------------------------------------------ */
FSStatus
fs_remove(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return FS_ERR_INVALID_PATH;
    }

    FsRawStat st;
    if (fs_plat_lstat(path, &st) != 0) {
        return fs_stat_util_status_from_errno(errno);
    }

    if (FS_S_ISDIR(st.mode)) {
        if (fs_plat_rmdir(path) == 0) {
            return FS_OK;
        }
        return fs_stat_util_status_from_errno(errno);
    }

    /* Regular files, symlinks, sockets, FIFOs, device nodes. */
    if (fs_plat_unlink(path) == 0) {
        return FS_OK;
    }
    return fs_stat_util_status_from_errno(errno);
}

/* ------------------------------------------------------------------ *
 * fs_copy
 *
 * Copies a regular file. The destination is overwritten if it exists.
 * Symbolic links are copied as links, not as their targets.
 * ------------------------------------------------------------------ */
FSStatus
fs_copy(const char *src_path, const char *dst_path)
{
    if (src_path == NULL || dst_path == NULL ||
        src_path[0] == '\0' || dst_path[0] == '\0') {
        return FS_ERR_INVALID_PATH;
    }

    FsRawStat st;
    if (fs_plat_lstat(src_path, &st) != 0) {
        return fs_stat_util_status_from_errno(errno);
    }

    /* Symlink: recreate the link itself. */
    if (FS_S_ISLNK(st.mode) || st.is_symlink) {
        char target[FS_PATH_MAX];
        if (fs_plat_readlink(src_path, target, sizeof(target)) != 0) {
            return fs_stat_util_status_from_errno(errno);
        }
        /* Remove an existing destination so the symlink call can succeed. */
        fs_plat_unlink(dst_path);
        if (fs_plat_symlink(target, dst_path) != 0) {
            return fs_stat_util_status_from_errno(errno);
        }
        return FS_OK;
    }

    if (!FS_S_ISREG(st.mode)) {
        errno = EINVAL;
        return FS_ERR_INVALID_PATH;
    }

    int in_fd = open(src_path, O_RDONLY | FS_O_BINARY);
    if (in_fd < 0) {
        return fs_stat_util_status_from_errno(errno);
    }

    int out_fd = open(dst_path, O_CREAT | O_WRONLY | O_TRUNC | FS_O_BINARY, 0644);
    if (out_fd < 0) {
        int e = errno;
        close(in_fd);
        errno = e;
        return fs_stat_util_status_from_errno(e);
    }

    char buf[65536];
    FSStatus result = FS_OK;
    for (;;) {
        ssize_t n = read(in_fd, buf, sizeof(buf));
        if (n < 0) {
            int e = errno;
            result = fs_stat_util_status_from_errno(e);
            break;
        }
        if (n == 0) break;

        ssize_t written = 0;
        while (written < n) {
            ssize_t w = write(out_fd, buf + written, (size_t)(n - written));
            if (w < 0) {
                int e = errno;
                result = fs_stat_util_status_from_errno(e);
                break;
            }
            written += w;
        }
        if (result != FS_OK) break;
    }

    close(in_fd);
    close(out_fd);

    /* Preserve the permission bits of the source file. */
    if (result == FS_OK) {
        unsigned int mode = st.mode & 07777u;
        if (mode != 0) {
            fs_plat_chmod(dst_path, mode);
        }
    }

    return result;
}
