/* ============================================================================
 * fs_stat_util.c -- Shared FileMetadata population helpers.
 * ============================================================================
 */

#include "fs_stat_util.h"
#include "format_utils.h"
#include "path_utils.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ *
 * Copy a string into a fixed-size field, truncating safely.
 * ------------------------------------------------------------------ */
static void
copy_str(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) return;
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    size_t n = strlen(src);
    if (n >= dst_size) n = dst_size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* ------------------------------------------------------------------ *
 * Map a POSIX errno value onto an FSStatus.
 * ------------------------------------------------------------------ */
FSStatus
fs_stat_util_status_from_errno(int e)
{
    switch (e) {
    case ENOENT:
    case ENOTDIR:
        return FS_ERR_NOT_FOUND;
    case EACCES:
    case EPERM:
    case EROFS:
        return FS_ERR_PERMISSION;
    case ENAMETOOLONG:
    case ELOOP:
    case EINVAL:
        return FS_ERR_INVALID_PATH;
    case EEXIST:
        return FS_ERR_EXISTS;
    case ENOMEM:
        return FS_ERR_NOMEM;
    default:
        return FS_ERR_IO;
    }
}

/* ------------------------------------------------------------------ *
 * fs_stat_util_fill
 *
 * Populate every FileMetadata field from a raw stat result. The caller is
 * responsible for having obtained `st` successfully.
 * ------------------------------------------------------------------ */
void
fs_stat_util_fill(const FsRawStat *st,
                  const char *full_path,
                  int resolve_target,
                  FileMetadata *out)
{
    out->status     = FS_OK;
    out->errno_code = 0;

    /* type -------------------------------------------------------------- */
    if      (FS_S_ISREG(st->mode))  out->type = FS_TYPE_REGULAR;
    else if (FS_S_ISDIR(st->mode))  out->type = FS_TYPE_DIRECTORY;
    else if (FS_S_ISLNK(st->mode))  out->type = FS_TYPE_SYMLINK;
    else if (FS_S_ISCHR(st->mode))  out->type = FS_TYPE_CHAR_DEV;
    else if (FS_S_ISBLK(st->mode))  out->type = FS_TYPE_BLOCK_DEV;
    else if (FS_S_ISFIFO(st->mode)) out->type = FS_TYPE_FIFO;
    else if (FS_S_ISSOCK(st->mode)) out->type = FS_TYPE_SOCKET;
    else                            out->type = FS_TYPE_UNKNOWN;

    /* sizes / ids ------------------------------------------------------- */
    out->size       = (off_t)st->size;
    out->inode      = st->inode;
    out->nlinks     = st->nlinks;
    out->blocks     = st->blocks;
    out->block_size = st->block_size;

    out->uid = st->uid;
    out->gid = st->gid;

    /* owner / group names ---------------------------------------------- */
    if (fs_plat_user_name(st->uid, out->owner, sizeof(out->owner)) != 0) {
        snprintf(out->owner, sizeof(out->owner), "%lu",
                 (unsigned long)st->uid);
    }
    if (fs_plat_group_name(st->gid, out->group, sizeof(out->group)) != 0) {
        snprintf(out->group, sizeof(out->group), "%lu",
                 (unsigned long)st->gid);
    }
    /* mode + permissions ----------------------------------------------- */
    out->mode = st->mode;
    fmt_perm_symbolic(st->mode, out->perm_symbolic, sizeof(out->perm_symbolic));
    fmt_perm_octal  (st->mode, out->perm_octal,   sizeof(out->perm_octal));

    /* timestamps -------------------------------------------------------- */
    out->atime = (time_t)st->atime;
    out->mtime = (time_t)st->mtime;
    out->ctime = (time_t)st->ctime;

    /* symlink target ---------------------------------------------------- */
    if (out->type == FS_TYPE_SYMLINK) {
        out->link_target[0]      = '\0';
        out->link_target_exists  = 0;

        if (fs_plat_readlink(full_path, out->link_target,
                             sizeof(out->link_target)) == 0) {
            if (resolve_target) {
                FsRawStat target_st;
                if (fs_plat_stat(full_path, &target_st) == 0) {
                    out->link_target_exists = 1;
                } else {
                    int e = errno;
                    out->link_target_exists = (e == ENOENT) ? -1 : 0;
                }
            }
        } else {
            out->link_target[0] = '\0';
        }
    } else {
        out->link_target[0]      = '\0';
        out->link_target_exists  = 0;
    }

    /* store the full path ------------------------------------------------ */
    if (full_path != NULL) {
        copy_str(out->path, sizeof(out->path), full_path);
    }
}

/* ------------------------------------------------------------------ *
 * fs_stat_util_fill_path
 *
 * Stat `full_path` (without following symlinks) and populate `out`.
 * On failure the error status is returned and `out` still carries the
 * name and path so the UI can render a "Permission denied" row.
 * ------------------------------------------------------------------ */
FSStatus
fs_stat_util_fill_path(const char *full_path,
                        const char *display_name,
                        int resolve_target,
                        FileMetadata *out)
{
    if (full_path == NULL || out == NULL) {
        return FS_ERR_INVALID_PATH;
    }

    /* Reset so a failed stat cannot leave stale data behind. */
    memset(out, 0, sizeof(*out));

    if (display_name != NULL) {
        copy_str(out->name, sizeof(out->name), display_name);
    } else {
        copy_str(out->name, sizeof(out->name), path_basename(full_path));
    }
    copy_str(out->path, sizeof(out->path), full_path);

    FsRawStat st;
    if (fs_plat_lstat(full_path, &st) != 0) {
        int e = errno;
        out->status     = fs_stat_util_status_from_errno(e);
        out->errno_code = e;
        out->type       = FS_TYPE_UNKNOWN;
        return out->status;
    }

    fs_stat_util_fill(&st, full_path, resolve_target, out);
    return FS_OK;
}
