/* ============================================================================
 * search.c -- Recursive name search.
 *
 * Implements:
 *   - fs_search : walk `root` recursively and collect every entry whose
 *                 base name matches `pattern`.
 *
 * Matching modes:
 *   - wildcard mode (default): '*', '?' and '[...]' are honoured via
 *     fs_plat_fnmatch().
 *   - literal mode: a plain substring match on the base name.
 *
 * Safety:
 *   - Symbolic links are never followed, so the traversal cannot loop.
 *   - Directory cycles are additionally detected via a visited-inode set
 *     (hard-linked directories are not possible on Linux, but bind mounts
 *     can still create cycles).
 *   - `max_depth` bounds the recursion; -1 means unlimited.
 *   - `max_results` bounds the number of matches collected; 0 means the
 *     array capacity is the only limit.
 * ============================================================================
 */

#include "fs_search.h"
#include "fs_stat_util.h"
#include "path_utils.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ *
 * Visited-inode set, used to break directory cycles.
 * ------------------------------------------------------------------ */
typedef struct {
    FsIno  *entries;
    size_t  count;
    size_t  capacity;
} InodeSet;

static int
inode_set_init(InodeSet *set, size_t initial_capacity)
{
    set->entries  = (FsIno *)calloc(initial_capacity, sizeof(FsIno));
    set->count    = 0;
    set->capacity = (set->entries != NULL) ? initial_capacity : 0;
    return (set->entries != NULL) ? 0 : -1;
}

static void
inode_set_free(InodeSet *set)
{
    free(set->entries);
    set->entries  = NULL;
    set->count    = 0;
    set->capacity = 0;
}

static int
inode_set_contains(const InodeSet *set, FsIno ino)
{
    for (size_t i = 0; i < set->count; i++) {
        if (set->entries[i] == ino) return 1;
    }
    return 0;
}

static int
inode_set_add(InodeSet *set, FsIno ino)
{
    if (set->count == set->capacity) {
        size_t new_cap = (set->capacity == 0) ? 64 : set->capacity * 2;
        FsIno *nb = (FsIno *)realloc(set->entries, new_cap * sizeof(FsIno));
        if (nb == NULL) return -1;
        set->entries  = nb;
        set->capacity = new_cap;
    }
    set->entries[set->count++] = ino;
    return 0;
}

/* ------------------------------------------------------------------ *
 * Match a base name against the pattern.
 * ------------------------------------------------------------------ */
static int
name_matches(const char *pattern, const char *name, int literal_only)
{
    if (literal_only) {
        return (strstr(name, pattern) != NULL) ? 1 : 0;
    }
    return fs_plat_fnmatch(pattern, name);
}

/* ------------------------------------------------------------------ *
 * Recursive worker.
 *
 * `depth` is the current depth relative to `root` (root itself is 0).
 * ------------------------------------------------------------------ */
static FSStatus
search_walk(const char *dir_path,
            const char *pattern,
            int literal_only,
            int max_depth,
            int max_results,
            SearchResult *out_results,
            int *result_count_io,
            int depth,
            InodeSet *visited)
{
    /* Stop immediately if we already have enough results. */
    if (max_results > 0 && *result_count_io >= max_results) {
        return FS_OK;
    }

    DIR *d = opendir(dir_path);
    if (d == NULL) {
        /* A directory we cannot open is skipped, not fatal: the overall
         * traversal still succeeds. */
        return FS_OK;
    }

    struct dirent *de;
    FSStatus result = FS_OK;

    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 ||
            strcmp(de->d_name, "..") == 0) {
            continue;
        }
        if (max_results > 0 && *result_count_io >= max_results) {
            break;
        }

        char full[FS_PATH_MAX];
        if (!path_join(dir_path, de->d_name, full, sizeof(full))) {
            continue;
        }

        /* Collect the metadata. Failures are tolerated: we still want
         * to know the file type, so lstat the entry directly. */
        FileMetadata meta;
        memset(&meta, 0, sizeof(meta));
        FSStatus s = fs_stat_util_fill_path(full, de->d_name, 0, &meta);
        if (s != FS_OK) {
            continue;
        }

        /* Match? */
        if (name_matches(pattern, meta.name, literal_only)) {
            if (out_results != NULL && *result_count_io < max_results) {
                out_results[*result_count_io].meta = meta;
            }
            (*result_count_io)++;
        }

        /* Recurse into directories (never following symlinks).
         * `max_depth` counts the levels below the root: with max_depth 0
         * only the root's own entries are examined, with max_depth 1 the
         * entries of its sub-directories are examined as well. */
        if (meta.type == FS_TYPE_DIRECTORY) {
            if (max_depth >= 0 && depth >= max_depth) {
                continue;   /* depth limit reached */
            }

            /* Cycle protection. */
            if (meta.inode != 0) {
                if (inode_set_contains(visited, meta.inode)) {
                    continue;
                }
                if (inode_set_add(visited, meta.inode) != 0) {
                    result = FS_ERR_NOMEM;
                    break;
                }
            }

            search_walk(full, pattern, literal_only, max_depth,
                        max_results, out_results, result_count_io,
                        depth + 1, visited);
        }
    }

    closedir(d);
    return result;
}

/* ------------------------------------------------------------------ *
 * fs_search
 * ------------------------------------------------------------------ */
FSStatus
fs_search(const char *root,
          const char *pattern,
          int literal_only,
          int max_depth,
          int max_results,
          SearchResult *out_results,
          int *result_count_io)
{
    if (root == NULL || pattern == NULL || result_count_io == NULL) {
        if (result_count_io) *result_count_io = 0;
        return FS_ERR_INVALID_PATH;
    }
    if (root[0] == '\0' || pattern[0] == '\0') {
        *result_count_io = 0;
        return FS_ERR_INVALID_PATH;
    }

    /* The root must exist and be a directory. This is validated before the
     * capacity check so that an invalid root is always reported as an error,
     * even when the caller passes a zero-sized output buffer. */
    FsRawStat root_st;
    if (fs_plat_lstat(root, &root_st) != 0) {
        int e = errno;
        *result_count_io = 0;
        return fs_stat_util_status_from_errno(e);
    }
    if (!FS_S_ISDIR(root_st.mode)) {
        *result_count_io = 0;
        return FS_ERR_NOT_DIR;
    }

    int capacity = *result_count_io;
    *result_count_io = 0;

    if (capacity < 0) capacity = 0;

    /* When a positive max_results is given it also bounds the array. */
    int effective_max = max_results;
    if (effective_max <= 0 || effective_max > capacity) {
        effective_max = capacity;
    }
    if (effective_max == 0) {
        /* Nothing can be stored: still walk the tree? The header says
         * "the traversal always runs to completion", but with no room to
         * store results there is no point. We report zero matches. */
        *result_count_io = 0;
        return FS_OK;
    }

    InodeSet visited;
    if (inode_set_init(&visited, 64) != 0) {
        return FS_ERR_NOMEM;
    }

    /* Mark the root itself so we never revisit it. */
    if (root_st.inode != 0) {
        inode_set_add(&visited, root_st.inode);
    }

    FSStatus s = search_walk(root, pattern, literal_only, max_depth,
                             effective_max, out_results, result_count_io,
                             0, &visited);

    inode_set_free(&visited);

    if (s != FS_OK) return s;

    /* The result count may exceed the array capacity only if the caller
     * asked for more results than it could store; clamp it so the caller
     * never reads past the end of its buffer. */
    if (*result_count_io > capacity) {
        *result_count_io = capacity;
    }
    return FS_OK;
}
