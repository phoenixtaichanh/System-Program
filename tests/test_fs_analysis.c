/* ============================================================================
 * test_fs_analysis.c -- Tests for the recursive directory analyzer.
 * ============================================================================
 */

#define _XOPEN_SOURCE 700

#include "test_util.h"
#include "fs_analysis.h"
#include "fs_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond) do { \
    if (cond) { g_pass++; } \
    else { \
        g_fail++; \
        fprintf(stderr, "  FAIL line %d: %s\n", __LINE__, #cond); \
    } \
} while (0)

#define SANDBOX t_sandbox_path()

static void
rm_sandbox(void)
{
    t_rmtree(SANDBOX);
}

/* Write a file with exactly `size` bytes. */
static void
make_file(const char *dir, const char *name, size_t size)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);

    FILE *f = fopen(path, "wb");
    if (f == NULL) return;

    for (size_t i = 0; i < size; i++) fputc('x', f);
    fclose(f);
}

static void
make_dir(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    t_mkdir(path);
}

/* ------------------------------------------------------------------ *
 * test_setup
 *
 *   SANDBOX/
 *     small.txt   (10 bytes)
 *     medium.txt  (100 bytes)
 *     big.txt     (1000 bytes)
 *     sub/
 *       a.txt     (50 bytes)
 *       subsub/
 *         b.txt   (500 bytes)
 * ------------------------------------------------------------------ */
static void
test_setup(void)
{
    rm_sandbox();
    t_mkdir(SANDBOX);

    make_file(SANDBOX, "small.txt", 10);
    make_file(SANDBOX, "medium.txt", 100);
    make_file(SANDBOX, "big.txt", 1000);

    make_dir(SANDBOX, "sub");
    make_file(SANDBOX, "sub/a.txt", 50);

    make_dir(SANDBOX, "sub/subsub");
    make_file(SANDBOX, "sub/subsub/b.txt", 500);
}

/* ------------------------------------------------------------------ *
 * test_basic_counts
 * ------------------------------------------------------------------ */
static void
test_basic_counts(void)
{
    DirStats stats;
    FSStatus s = fs_analyze_directory(SANDBOX, &stats);
    CHECK(s == FS_OK);

    /* 5 files: small, medium, big, a, b */
    CHECK(stats.total_files == 5);
    /* 2 sub-directories: sub and sub/subsub (the root itself is not
     * counted) */
    CHECK(stats.total_dirs == 2);
    CHECK(stats.total_symlinks == 0);

    /* Total size: 10 + 100 + 1000 + 50 + 500 = 1660 */
    CHECK(stats.total_bytes == 1660);

    /* The average file size is total / files. */
    CHECK(stats.avg_file_size == 1660 / 5);
}
/* ------------------------------------------------------------------ *
 * test_largest_file
 * ------------------------------------------------------------------ */
static void
test_largest_file(void)
{
    DirStats stats;
    CHECK(fs_analyze_directory(SANDBOX, &stats) == FS_OK);

    /* The largest file is big.txt with 1000 bytes. */
    CHECK(stats.largest_file_size == 1000);
    CHECK(strstr(stats.largest_file_path, "big.txt") != NULL);

    /* The top-files list must be sorted descending. */
    CHECK(stats.top_files_count > 0);
    CHECK(stats.top_files_count <= FS_TOP_FILES_MAX);
    for (int i = 1; i < stats.top_files_count; i++) {
        CHECK(stats.top_files[i - 1].size >= stats.top_files[i].size);
    }
    /* The first entry is the largest file. */
    CHECK(stats.top_files[0].size == 1000);
}

/* ------------------------------------------------------------------ *
 * test_subdirs
 * ------------------------------------------------------------------ */
static void
test_subdirs(void)
{
    DirStats stats;
    CHECK(fs_analyze_directory(SANDBOX, &stats) == FS_OK);

    CHECK(stats.subdirs_count > 0);
    CHECK(stats.subdirs_count <= FS_TOP_DIRS_MAX);

    /* The sub-directory list must be sorted descending by size. */
    for (int i = 1; i < stats.subdirs_count; i++) {
        CHECK(stats.subdirs[i - 1].size >= stats.subdirs[i].size);
    }
}

/* ------------------------------------------------------------------ *
 * test_empty_dir
 * ------------------------------------------------------------------ */
static void
test_empty_dir(void)
{
    char empty[512];
    snprintf(empty, sizeof(empty), "%s_empty", SANDBOX);
    t_mkdir(empty);

    DirStats stats;
    FSStatus s = fs_analyze_directory(empty, &stats);
    CHECK(s == FS_OK);
    CHECK(stats.total_files == 0);
    CHECK(stats.total_bytes == 0);
    CHECK(stats.top_files_count == 0);
    CHECK(stats.largest_file_size == 0);

    t_rmdir(empty);
}

/* ------------------------------------------------------------------ *
 * test_errors
 * ------------------------------------------------------------------ */
static void
test_errors(void)
{
    DirStats stats;
    CHECK(fs_analyze_directory(NULL, &stats) != FS_OK);
    CHECK(fs_analyze_directory("/nonexistent/path/xyz", &stats) != FS_OK);
    CHECK(fs_analyze_directory(SANDBOX, NULL) != FS_OK);
}

/* ------------------------------------------------------------------ *
 * test_many_files
 *
 * The top-files list must stay within its bounds even when there are
 * more files than it can hold.
 * ------------------------------------------------------------------ */
static void
test_many_files(void)
{
    char many[512];
    snprintf(many, sizeof(many), "%s_many", SANDBOX);
    t_mkdir(many);

    char name[64];
    for (int i = 0; i < FS_TOP_FILES_MAX + 20; i++) {
        snprintf(name, sizeof(name), "file_%03d.dat", i);
        make_file(many, name, (size_t)(i + 1));
    }

    DirStats stats;
    FSStatus s = fs_analyze_directory(many, &stats);
    CHECK(s == FS_OK);
    CHECK(stats.total_files == FS_TOP_FILES_MAX + 20);
    CHECK(stats.top_files_count == FS_TOP_FILES_MAX);
    CHECK(stats.top_files_count <= FS_TOP_FILES_MAX);

    /* The list must still be sorted descending and free of duplicates. */
    for (int i = 1; i < stats.top_files_count; i++) {
        CHECK(stats.top_files[i - 1].size >= stats.top_files[i].size);
    }

    /* The largest file is the last one created (size 40). */
    CHECK(stats.largest_file_size == FS_TOP_FILES_MAX + 20);

    rm_sandbox();
    t_mkdir(many);
    t_rmdir(many);
}

/* ------------------------------------------------------------------ *
 * main
 * ------------------------------------------------------------------ */
int
main(void)
{
    printf("==== fs_analysis test ====\n");
    t_sandbox_init("analysis");

    test_setup();

    test_basic_counts();
    test_largest_file();
    test_subdirs();
    test_empty_dir();
    test_errors();
    test_many_files();

    rm_sandbox();

    printf("==== Result: %d passed, %d failed ====\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
