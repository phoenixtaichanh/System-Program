/* ============================================================================
 * listing.c -- Directory listing service.
 *
 * Implements:
 *   - fs_list_dir  : enumerate a directory via opendir/readdir, populate a
 *                    caller-provided buffer. Each entry is lstat'ed so
 *                    symlinks are reported as symlinks. Permissions, owner
 *                    and timestamps are filled so the GUI can render columns
 *                    in one pass.
 *
 * Notes
 *   - We skip "." and "..".
 *   - Entries that fail to lstat() are still appended (with status set), so
 *     the GUI can show "Permission denied" rows instead of dropping data.
 *   - Results are sorted by name (case-sensitive, ascending).
 * ============================================================================
 */

#define _XOPEN_SOURCE 700   /* expose S_IF* / S_IS* macros */

#include "fs_listing.h"
#include "path_utils.h"
#include "format_utils.h"

#include <dirent.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ------------------------------------------------------------------ *
 * Local helper: fill a FileMetadata from a populated struct stat.
 * Returns FS_OK, or FS_ERR_NOT_FOUND / FS_ERR_PERMISSION if the stat
 * call itself failed (caller can decide what to do).
 * ------------------------------------------------------------------ */
static FSStatus
fill_metadata_from_lstat(const char *full_path,
                         const struct stat *st,
                         FileMetadata *out)
{
    out->status = FS_OK;
    out->errno_code = 0;

    /* type -------------------------------------------------------------- */
    if      (S_ISREG(st->st_mode))  out->type = FS_TYPE_REGULAR;
    else if (S_ISDIR(st->st_mode))  out->type = FS_TYPE_DIRECTORY;
    else if (S_ISLNK(st->st_mode))  out->type = FS_TYPE_SYMLINK;
    else if (S_ISCHR(st->st_mode))  out->type = FS_TYPE_CHAR_DEV;
    else if (S_ISBLK(st->st_mode))  out->type = FS_TYPE_BLOCK_DEV;
    else if (S_ISFIFO(st->st_mode)) out->type = FS_TYPE_FIFO;
    else if (S_ISSOCK(st->st_mode)) out->type = FS_TYPE_SOCKET;
    else                            out->type = FS_TYPE_UNKNOWN;

    /* sizes / ids ------------------------------------------------------- */
    out->size       = st->st_size;
    out->inode      = st->st_ino;
    out->nlinks     = st->st_nlinks;
    out->blocks     = st->st_blocks;
    out->block_size = st->st_blksize;

    out->uid = st->st_uid;
    out->gid = st->st_gid;

    /* owner / group names (NULL-safe) */
    struct passwd *pw = getpwuid(st->st_uid);
    if (pw && pw->pw_name) {
        snprintf(out->owner, sizeof(out->owner), "%s", pw->pw_name);
    } else {
        snprintf(out->owner, sizeof(out->owner), "%lu",
                 (unsigned long)st->st_uid);
    }
    struct group *gr = getgrgid(st->st_gid);
    if (gr && gr->gr_name) {
        snprintf(out->group, sizeof(out->group), "%s", gr->gr_name);
    } else {
        snprintf(out->group, sizeof(out->group), "%lu",
                 (unsigned long)st->st_gid);
    }

    /* mode + permissions ----------------------------------------------- */
    out->mode = st->st_mode;
    fmt_perm_symbolic(st->st_mode, out->perm_symbolic, sizeof(out->perm_symbolic));
    fmt_perm_octal  (st->st_mode, out->perm_octal,   sizeof(out->perm_octal));

    /* timestamps */
    out->atime = st->st_atime;
    out->mtime = st->st_mtime;
    out->ctime = st->st_ctime;

    /* symlink target (only if symlink) */
    if (S_ISLNK(st->st_mode)) {
        ssize_t n = readlink(full_path, out->link_target,
                             sizeof(out->link_target) - 1);
        if (n < 0) {
            out->link_target[0] = '\0';
            out->link_target_exists = 0;
        } else {
            out->link_target[n] = '\0';
            struct stat target_st;
            if (stat(full_path, &target_st) == 0) out->link_target_exists = 1;
            else if (errno == ENOENT)              out->link_target_exists = -1;
            else                                   out->link_target_exists = 0;
        }
    } else {
        out->link_target[0] = '\0';
        out->link_target_exists = 0;
    }

    /* store the full path in `path` if not already set */
    if (out->path[0] == '\0') {
        snprintf(out->path, sizeof(out->path), "%s", full_path);
    }

    return FS_OK;
}

/* ------------------------------------------------------------------ *
 * Local helper: map a dirent's name to a DirEntry using lstat.
 * Returns FS_OK if stat succeeded, otherwise an error status
 * (still leaves a placeholder entry in `out`).
 * ------------------------------------------------------------------ */
static FSStatus
stat_one_entry(const char *parent, const char *name,
               DirEntry *out)
{
    char full[FS_PATH_MAX];
    if (!path_join(parent, name, full, sizeof(full))) {
        out->meta.status = FS_ERR_INVALID_PATH;
        out->meta.errno_code = ENAMETOOLONG;
        return FS_ERR_INVALID_PATH;
    }

    /* copy name + path first so even on failure we render something */
    snprintf(out->meta.name, sizeof(out->meta.name), "%s", name);
    snprintf(out->meta.path, sizeof(out->meta.path), "%s", full);

    struct stat st;
    if (lstat(full, &st) != 0) {
        int e = errno;
        out->meta.status = (e == EACCES || e == EPERM) ? FS_ERR_PERMISSION
                                                        : FS_ERR_IO;
        out->meta.errno_code = e;
        return out->meta.status;
    }
    return fill_metadata_from_lstat(full, &st, &out->meta);
}

/* ------------------------------------------------------------------ *
 * qsort comparator: by name ascending (case-sensitive byte order).
 * ------------------------------------------------------------------ */
static int
cmp_dirent_name(const void *a, const void *b)
{
    const DirEntry *ea = (const DirEntry *)a;
    const DirEntry *eb = (const DirEntry *)b;
    return strcmp(ea->meta.name, eb->meta.name);
}

/* ------------------------------------------------------------------ *
 * fs_list_dir
 * ------------------------------------------------------------------ */
FSStatus
fs_list_dir(const char *path, DirEntry *out_entries, int *count_io)
{
    if (path == NULL || out_entries == NULL || count_io == NULL) {
        if (count_io) *count_io = 0;
        return FS_ERR_INVALID_PATH;
    }

    int capacity = *count_io;
    *count_io = 0;
    if (capacity <= 0) return FS_OK;

    DIR *d = opendir(path);
    if (d == NULL) {
        int e = errno;
        if (e == ENOENT || e == ENOTDIR) return FS_ERR_NOT_FOUND;
        if (e == EACCES || e == EPERM)  return FS_ERR_PERMISSION;
        return FS_ERR_IO;
    }

    struct dirent *de;
    int written = 0;
    int first_err = FS_OK;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 ||
            strcmp(de->d_name, "..") == 0) {
            continue;
        }
        if (written >= capacity) {
            /* ran out of room; stop. */
            break;
        }
        DirEntry *dst = &out_entries[written];
        memset(dst, 0, sizeof(*dst));
        FSStatus s = stat_one_entry(path, de->d_name, dst);
        if (s != FS_OK && first_err == FS_OK) first_err = s;
        written++;
    }
    closedir(d);

    *count_io = written;

    /* sort the populated slice */
    if (written > 1) {
        qsort(out_entries, (size_t)written, sizeof(DirEntry), cmp_dirent_name);
    }

    /* If we wrote entries but the dir itself was openable (so we got
     * this far), we treat ENOENT/ENOTDIR as the dominant error even if
     * some individual entries also failed; otherwise report the first
     * per-entry error. */
    if (written == 0) {
        /* Nothing was enumerable - this happens if directory is empty OR
         * readdir returned NULL after first call due to I/O error. */
        return first_err == FS_OK ? FS_OK : first_err;
    }
    return FS_OK;
}