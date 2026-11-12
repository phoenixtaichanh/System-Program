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
 *   - All platform-specific syscalls are routed through fs_platform.h.
 * ============================================================================
 */

#include "fs_listing.h"
#include "path_utils.h"
#include "format_utils.h"
#include "fs_stat_util.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ *
 * Copy a name into a fixed-size field, truncating safely.
 *
 * snprintf() already truncates, but GCC warns about it when the source
 * length is not provably bounded. This helper takes the source as a
 * plain "const char *" so the warning disappears.
 * ------------------------------------------------------------------ */
static void
copy_name(char *dst, size_t dst_size, const char *src)
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
 * Growable buffer used while the whole directory is being collected.
 * ------------------------------------------------------------------ */
typedef struct {
    DirEntry *data;
    size_t    count;
    size_t    capacity;
} EntryVec;

static int
entry_vec_init(EntryVec *v)
{
    v->data     = NULL;
    v->count    = 0;
    v->capacity = 0;
    return 0;
}

static void
entry_vec_free(EntryVec *v)
{
    free(v->data);
    v->data     = NULL;
    v->count    = 0;
    v->capacity = 0;
}

static DirEntry *
entry_vec_push(EntryVec *v)
{
    if (v->count == v->capacity) {
        size_t new_cap = (v->capacity == 0) ? 64 : v->capacity * 2;
        DirEntry *nb = (DirEntry *)realloc(v->data,
                                           new_cap * sizeof(DirEntry));
        if (nb == NULL) return NULL;
        v->data     = nb;
        v->capacity = new_cap;
    }
    DirEntry *slot = &v->data[v->count++];
    memset(slot, 0, sizeof(*slot));
    return slot;
}

/* ------------------------------------------------------------------ *
 * fs_list_dir
 *
 * The whole directory is read and sorted first; only then is the
 * requested prefix handed back to the caller. Sorting a truncated slice
 * would produce a non-deterministic order, because readdir() returns
 * entries in whatever order the file system stores them.
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
        return fs_stat_util_status_from_errno(e);
    }

    EntryVec vec;
    entry_vec_init(&vec);

    struct dirent *de;
    int first_err = FS_OK;

    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 ||
            strcmp(de->d_name, "..") == 0) {
            continue;
        }

        char full[FS_PATH_MAX];
        if (!path_join(path, de->d_name, full, sizeof(full))) {
            /* Path too long: still record the entry so the user sees it. */
            DirEntry *dst = entry_vec_push(&vec);
            if (dst == NULL) {
                first_err = FS_ERR_NOMEM;
                break;
            }
            copy_name(dst->meta.name, sizeof(dst->meta.name), de->d_name);
            snprintf(dst->meta.path, sizeof(dst->meta.path), "%s/%s",
                     path, de->d_name);
            dst->meta.status     = FS_ERR_INVALID_PATH;
            dst->meta.errno_code = ENAMETOOLONG;
            dst->meta.type       = FS_TYPE_UNKNOWN;
            continue;
        }

        DirEntry *dst = entry_vec_push(&vec);
        if (dst == NULL) {
            first_err = FS_ERR_NOMEM;
            break;
        }

        FSStatus s = fs_stat_util_fill_path(full, de->d_name, 1, &dst->meta);
        if (s != FS_OK && first_err == FS_OK) {
            first_err = s;
        }
    }

    if (errno != 0 && vec.count == 0) {
        /* readdir() itself failed (as opposed to simply reaching the end
         * of the directory, which also leaves errno untouched). */
        int e = errno;
        closedir(d);
        entry_vec_free(&vec);
        *count_io = 0;
        return fs_stat_util_status_from_errno(e);
    }

    closedir(d);

    if (first_err == FS_ERR_NOMEM) {
        entry_vec_free(&vec);
        *count_io = 0;
        return FS_ERR_NOMEM;
    }

    /* Sort the complete set of entries by name. */
    if (vec.count > 1) {
        qsort(vec.data, vec.count, sizeof(DirEntry), cmp_dirent_name);
    }

    /* Hand back the first `capacity` entries. */
    size_t to_copy = vec.count;
    if (to_copy > (size_t)capacity) {
        to_copy = (size_t)capacity;
    }
    if (to_copy > 0) {
        memcpy(out_entries, vec.data, to_copy * sizeof(DirEntry));
    }
    *count_io = (int)to_copy;

    entry_vec_free(&vec);

    /* The directory itself was enumerable, so a per-entry failure is not
     * fatal: report FS_OK and let the individual rows carry their own
     * status. Only an entirely empty result with a recorded error is
     * surfaced as a failure. */
    if (to_copy == 0 && first_err != FS_OK) {
        return first_err;
    }
    return FS_OK;
}
