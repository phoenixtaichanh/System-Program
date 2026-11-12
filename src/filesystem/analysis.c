/* ============================================================================
 * analysis.c -- Recursive directory analyzer.
 *
 * Implements:
 *   - fs_analyze_directory : walk a directory tree and accumulate counts,
 *                             total size, the largest file, the top-N
 *                             largest files and the per-subdirectory totals.
 *
 * Design notes:
 *   - Symbolic links are NOT followed (no infinite loops, no double
 *     counting of the target).
 *   - The traversal always completes even if some sub-trees cannot be
 *     read; those sub-trees are counted as "skipped" but do not fail the
 *     whole analysis.
 *   - Top-N lists are kept sorted in descending order by size.
 * ============================================================================
 */

#include "fs_analysis.h"
#include "fs_stat_util.h"
#include "path_utils.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ *
 * Copy a name into a fixed-size field, truncating safely.
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
 * Insert `size`/`path` into the top-files list, keeping it sorted by
 * descending size. Only the first `FS_TOP_FILES_MAX` entries are kept.
 * ------------------------------------------------------------------ */
static void
top_files_insert(DirStats *stats, const char *path, unsigned long long size)
{
    /* Find the insertion point: the first entry that is smaller than the
     * new one. Everything from there on is shifted one slot down. */
    int pos = 0;
    while (pos < stats->top_files_count &&
           stats->top_files[pos].size >= size) {
        pos++;
    }

    /* The list is full and the new entry does not make the cut. */
    if (pos >= FS_TOP_FILES_MAX) return;

    /* Shift the entries after the insertion point one slot down.
     * The loop must stop at FS_TOP_FILES_MAX - 1: writing to
     * top_files[FS_TOP_FILES_MAX] would run past the end of the array. */
    int last = stats->top_files_count;
    if (last > FS_TOP_FILES_MAX - 1) last = FS_TOP_FILES_MAX - 1;
    for (int i = last; i > pos; i--) {
        stats->top_files[i] = stats->top_files[i - 1];
    }

    snprintf(stats->top_files[pos].path, FS_PATH_MAX, "%s", path);
    stats->top_files[pos].size = size;

    if (stats->top_files_count < FS_TOP_FILES_MAX) {
        stats->top_files_count++;
    }
}

/* ------------------------------------------------------------------ *
 * Insert a subdirectory total into the subdirs list, keeping it sorted
 * by descending size.
 * ------------------------------------------------------------------ */
static void
subdirs_insert(DirStats *stats, const char *name, unsigned long long size)
{
    /* Find the insertion point: the first entry that is smaller than the
     * new one. */
    int pos = 0;
    while (pos < stats->subdirs_count &&
           stats->subdirs[pos].size >= size) {
        pos++;
    }

    if (pos >= FS_TOP_DIRS_MAX) return;

    /* Shift down, but never past the last valid slot. */
    int last = stats->subdirs_count;
    if (last > FS_TOP_DIRS_MAX - 1) last = FS_TOP_DIRS_MAX - 1;
    for (int i = last; i > pos; i--) {
        stats->subdirs[i] = stats->subdirs[i - 1];
    }

    copy_name(stats->subdirs[pos].name, FS_NAME_MAX, name);
    stats->subdirs[pos].size = size;

    if (stats->subdirs_count < FS_TOP_DIRS_MAX) {
        stats->subdirs_count++;
    }
}

/* ------------------------------------------------------------------ *
 * Recursive worker.
 *
 * `out` accumulates the global totals.
 * `subdir_size` accumulates the size of the current subtree so the
 * caller can report a per-subdirectory total.
 * ------------------------------------------------------------------ */
static FSStatus
analyze_walk(const char *dir_path,
             DirStats *out,
             unsigned long long *subdir_size,
             int depth,
             int *skipped_dirs)
{
    /* Guard against runaway recursion on pathological trees. */
    if (depth > 512) {
        return FS_OK;
    }

    DIR *d = opendir(dir_path);
    if (d == NULL) {
        (*skipped_dirs)++;
        return FS_OK;
    }

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 ||
            strcmp(de->d_name, "..") == 0) {
            continue;
        }

        char full[FS_PATH_MAX];
        if (!path_join(dir_path, de->d_name, full, sizeof(full))) {
            continue;
        }

        FsRawStat st;
        if (fs_plat_lstat(full, &st) != 0) {
            continue;   /* entry vanished or is inaccessible */
        }

        if (FS_S_ISLNK(st.mode) || st.is_symlink) {
            out->total_symlinks++;
            /* The link itself occupies a few bytes; its target is not
             * counted, so the total reflects the tree, not the world. */
            out->total_bytes += (unsigned long long)st.size;
            *subdir_size      += (unsigned long long)st.size;
            continue;
        }

        if (FS_S_ISDIR(st.mode)) {
            out->total_dirs++;

            unsigned long long child_size = 0;
            analyze_walk(full, out, &child_size, depth + 1, skipped_dirs);

            /* The recursion already added the subtree's files to
             * out->total_bytes, so only the per-directory total has to
             * be accumulated here. Adding child_size to total_bytes as
             * well would count every file twice. */
            *subdir_size += child_size;

            subdirs_insert(out, de->d_name, child_size);
            continue;
        }

        if (FS_S_ISREG(st.mode)) {
            out->total_files++;
            out->total_bytes += (unsigned long long)st.size;
            *subdir_size      += (unsigned long long)st.size;

            if ((unsigned long long)st.size > out->largest_file_size) {
                out->largest_file_size = (unsigned long long)st.size;
                snprintf(out->largest_file_path, FS_PATH_MAX, "%s", full);
            }
            top_files_insert(out, full, (unsigned long long)st.size);
            continue;
        }

        /* Sockets, FIFOs, device nodes: counted but not sized. */
        out->total_other++;
    }

    closedir(d);
    return FS_OK;
}

/* ------------------------------------------------------------------ *
 * fs_analyze_directory
 * ------------------------------------------------------------------ */
FSStatus
fs_analyze_directory(const char *path, DirStats *out_stats)
{
    if (path == NULL || out_stats == NULL) {
        return FS_ERR_INVALID_PATH;
    }
    if (path[0] == '\0') {
        return FS_ERR_INVALID_PATH;
    }

    memset(out_stats, 0, sizeof(*out_stats));
    snprintf(out_stats->path, FS_PATH_MAX, "%s", path);

    /* The root must exist and be a directory. */
    FsRawStat root_st;
    if (fs_plat_lstat(path, &root_st) != 0) {
        return fs_stat_util_status_from_errno(errno);
    }
    if (!FS_S_ISDIR(root_st.mode)) {
        return FS_ERR_NOT_DIR;
    }

    int skipped = 0;
    unsigned long long root_size = 0;

    FSStatus s = analyze_walk(path, out_stats, &root_size, 0, &skipped);
    if (s != FS_OK) {
        return s;
    }

    /* Average file size (0 when there are no files). */
    if (out_stats->total_files > 0) {
        out_stats->avg_file_size =
            out_stats->total_bytes / out_stats->total_files;
    } else {
        out_stats->avg_file_size = 0;
    }

    return FS_OK;
}
