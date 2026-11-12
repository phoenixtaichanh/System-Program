/* ============================================================================
 * test_fs_operations.c -- Tests for the safe file system operations.
 *
 * Covers fs_mkdir, fs_touch, fs_rename, fs_remove and fs_copy.
 * ============================================================================
 */

#define _XOPEN_SOURCE 700

#include "test_util.h"
#include "fs_operations.h"
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

static int
path_exists(const char *path)
{
    return t_exists(path);
}

static int
is_dir(const char *path)
{
    return t_is_dir(path);
}

static int
is_file(const char *path)
{
    return t_is_reg(path);
}

/* ------------------------------------------------------------------ *
 * test_mkdir
 * ------------------------------------------------------------------ */
static void
test_mkdir(void)
{
    rm_sandbox();
    t_mkdir(SANDBOX);

    char path[512];
    snprintf(path, sizeof(path), "%s/simple", SANDBOX);

    CHECK(fs_mkdir(path, 0) == FS_OK);
    CHECK(is_dir(path) == 1);

    /* Creating it again must fail (no -p). */
    CHECK(fs_mkdir(path, 0) != FS_OK);

    /* With recursive the existing directory is accepted. */
    CHECK(fs_mkdir(path, 1) == FS_OK);

    /* Recursive creation of nested directories. */
    char nested[512];
    snprintf(nested, sizeof(nested), "%s/a/b/c", SANDBOX);
    CHECK(fs_mkdir(nested, 1) == FS_OK);
    CHECK(is_dir(nested) == 1);

    /* Without recursive, the nested creation must fail. */
    char nested2[512];
    snprintf(nested2, sizeof(nested2), "%s/x/y", SANDBOX);
    CHECK(fs_mkdir(nested2, 0) != FS_OK);

    /* NULL argument. */
    CHECK(fs_mkdir(NULL, 0) != FS_OK);
}

/* ------------------------------------------------------------------ *
 * test_touch
 * ------------------------------------------------------------------ */
static void
test_touch(void)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/touched.txt", SANDBOX);

    CHECK(fs_touch(path) == FS_OK);
    CHECK(is_file(path) == 1);

    /* The file must be empty. */
    struct stat st;
    CHECK(stat(path, &st) == 0);
    CHECK(st.st_size == 0);

    /* Touching an existing file must fail (O_EXCL). */
    CHECK(fs_touch(path) != FS_OK);

    CHECK(fs_touch(NULL) != FS_OK);
}

/* ------------------------------------------------------------------ *
 * test_rename
 * ------------------------------------------------------------------ */
static void
test_rename(void)
{
    char old_path[512], new_path[512];
    snprintf(old_path, sizeof(old_path), "%s/old.txt", SANDBOX);
    snprintf(new_path, sizeof(new_path), "%s/new.txt", SANDBOX);

    CHECK(t_write_file(old_path, "content\n") == 0);

    CHECK(fs_rename(old_path, new_path) == FS_OK);
    CHECK(path_exists(old_path) == 0);
    CHECK(is_file(new_path) == 1);

    /* The content must survive the rename. */
    FILE *f = fopen(new_path, "rb");
    CHECK(f != NULL);
    if (f != NULL) {
        char buf[64] = {0};
        CHECK(fgets(buf, sizeof(buf), f) != NULL);
        CHECK(strcmp(buf, "content\n") == 0);
        fclose(f);
    }

    /* Renaming a missing file must fail. */
    CHECK(fs_rename("/nonexistent/xyz", new_path) != FS_OK);
    CHECK(fs_rename(NULL, NULL) != FS_OK);
}

/* ------------------------------------------------------------------ *
 * test_remove
 * ------------------------------------------------------------------ */
static void
test_remove(void)
{
    char file[512];
    snprintf(file, sizeof(file), "%s/removable.txt", SANDBOX);

    CHECK(t_write_file(file, "x\n") == 0);

    CHECK(fs_remove(file) == FS_OK);
    CHECK(path_exists(file) == 0);

    /* Removing a missing path must fail. */
    CHECK(fs_remove(file) != FS_OK);
    CHECK(fs_remove(NULL) != FS_OK);
}

/* ------------------------------------------------------------------ *
 * test_remove_nonempty_dir
 *
 * The safe remove must refuse to delete a directory that still has
 * content.
 * ------------------------------------------------------------------ */
static void
test_remove_nonempty_dir(void)
{
    char dir[512], inner[512];
    snprintf(dir, sizeof(dir), "%s/nonempty", SANDBOX);
    snprintf(inner, sizeof(inner), "%s/nonempty/file.txt", SANDBOX);

    t_mkdir(dir);
    CHECK(t_write_file(inner, "x\n") == 0);

    /* The removal must fail and the directory must survive. */
    CHECK(fs_remove(dir) != FS_OK);
    CHECK(is_dir(dir) == 1);

    /* Once emptied, it can be removed. */
    t_unlink(inner);
    CHECK(fs_remove(dir) == FS_OK);
    CHECK(path_exists(dir) == 0);
}

/* ------------------------------------------------------------------ *
 * test_copy
 * ------------------------------------------------------------------ */
static void
test_copy(void)
{
    char src[512], dst[512];
    snprintf(src, sizeof(src), "%s/source.txt", SANDBOX);
    snprintf(dst, sizeof(dst), "%s/copied.txt", SANDBOX);

    CHECK(t_write_file(src, "hello copy\n") == 0);

    CHECK(fs_copy(src, dst) == FS_OK);
    CHECK(is_file(dst) == 1);

    /* The content must be identical. */
    FILE *f = fopen(dst, "rb");
    CHECK(f != NULL);
    if (f != NULL) {
        char buf[64] = {0};
        CHECK(fgets(buf, sizeof(buf), f) != NULL);
        CHECK(strcmp(buf, "hello copy\n") == 0);
        fclose(f);
    }

    /* The source must still exist. */
    CHECK(is_file(src) == 1);

    /* Copying a missing source must fail. */
    CHECK(fs_copy("/nonexistent/xyz", dst) != FS_OK);
    CHECK(fs_copy(NULL, NULL) != FS_OK);
}

/* ------------------------------------------------------------------ *
 * main
 * ------------------------------------------------------------------ */
int
main(void)
{
    printf("==== fs_operations test ====\n");
    t_sandbox_init("operations");

    test_mkdir();
    test_touch();
    test_rename();
    test_remove();
    test_remove_nonempty_dir();
    test_copy();

    rm_sandbox();

    printf("==== Result: %d passed, %d failed ====\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
