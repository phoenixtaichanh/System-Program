/* ============================================================================
 * test_fs_listing.c -- Smoke test for fs_list_dir + fs_navigation.
 *
 * Build (one binary per test source file):
 *   make build/test_fs_listing
 *
 * Run:
 *   ./build/test_fs_listing
 *
 * Exit code 0 = all assertions passed.
 * Exit code 1 = at least one assertion failed.
 * ============================================================================
 */

#define _XOPEN_SOURCE 700

#include "test_util.h"
#include "fs_listing.h"
#include "fs_navigation.h"
#include "fs_types.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* ------------------------------------------------------------------ *
 * Test helpers
 * ------------------------------------------------------------------ */
static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond) do { \
    if (cond) { g_pass++; } \
    else { \
        g_fail++; \
        fprintf(stderr, "  FAIL line %d: %s\n", __LINE__, #cond); \
    } \
} while (0)

#define CHECK_STR_EQ(actual, expected) do { \
    if (strcmp((actual), (expected)) == 0) { g_pass++; } \
    else { \
        g_fail++; \
        fprintf(stderr, "  FAIL line %d: \"%s\" != \"%s\"\n", \
                __LINE__, (actual), (expected)); \
    } \
} while (0)

/* ------------------------------------------------------------------ *
 * Helpers for setting up an isolated test directory under /tmp
 * ------------------------------------------------------------------ */
#define SANDBOX t_sandbox_path()

static void
rm_sandbox(void)
{
    t_rmtree(SANDBOX);
}

static void
make_sandbox(void)
{
    rm_sandbox();

    char path[512];

    t_mkdir(SANDBOX);

    snprintf(path, sizeof(path), "%s/sub1", SANDBOX);
    t_mkdir(path);
    snprintf(path, sizeof(path), "%s/sub2", SANDBOX);
    t_mkdir(path);

    snprintf(path, sizeof(path), "%s/fileA.txt", SANDBOX);
    t_write_file(path, "A\n");
    snprintf(path, sizeof(path), "%s/fileB.txt", SANDBOX);
    t_write_file(path, "B\n");
    snprintf(path, sizeof(path), "%s/sub1/inner.txt", SANDBOX);
    t_write_file(path, "hi\n");
}

/* ------------------------------------------------------------------ *
 * test_list_dir_basic
 * ------------------------------------------------------------------ */
static void
test_list_dir_basic(void)
{
    printf("test_list_dir_basic\n");
    DirEntry entries[64];
    int n = (int)(sizeof(entries) / sizeof(entries[0]));

    FSStatus s = fs_list_dir(SANDBOX, entries, &n);
    CHECK(s == FS_OK);
    /* We created: sub1, sub2, fileA.txt, fileB.txt = 4 entries */
    CHECK(n == 4);

    /* Sorted by name: fileA.txt, fileB.txt, sub1, sub2 */
    CHECK_STR_EQ(entries[0].meta.name, "fileA.txt");
    CHECK_STR_EQ(entries[1].meta.name, "fileB.txt");
    CHECK_STR_EQ(entries[2].meta.name, "sub1");
    CHECK_STR_EQ(entries[3].meta.name, "sub2");

    /* Type checks */
    CHECK(entries[0].meta.type == FS_TYPE_REGULAR);
    CHECK(entries[2].meta.type == FS_TYPE_DIRECTORY);

    /* Permissions should be filled */
    CHECK(strlen(entries[0].meta.perm_symbolic) == 10);
    CHECK(strlen(entries[0].meta.perm_octal)   == 4);

    /* Owner's name should be filled - at least non-empty */
    CHECK(entries[0].meta.owner[0] != '\0');

    /* mtime should be a sane value (after 2000) */
    CHECK(entries[0].meta.mtime > 946684800);   /* 2000-01-01 */
}

/* ------------------------------------------------------------------ *
 * test_list_dir_nonexistent
 * ------------------------------------------------------------------ */
static void
test_list_dir_nonexistent(void)
{
    printf("test_list_dir_nonexistent\n");
    DirEntry entries[8];
    int n = (int)(sizeof(entries) / sizeof(entries[0]));

    FSStatus s = fs_list_dir("/this/path/does/not/exist/12345", entries, &n);
    CHECK(s == FS_ERR_NOT_FOUND);
    CHECK(n == 0);
}

/* ------------------------------------------------------------------ *
 * test_list_dir_file_not_dir
 *   Listing a regular file should fail with FS_ERR_NOT_DIR.
 * ------------------------------------------------------------------ */
static void
test_list_dir_file_not_dir(void)
{
    printf("test_list_dir_file_not_dir\n");
    DirEntry entries[8];
    int n = (int)(sizeof(entries) / sizeof(entries[0]));

    FSStatus s = fs_list_dir("/etc/hostname", entries, &n);
    CHECK(s == FS_ERR_NOT_FOUND || s == FS_ERR_NOT_DIR);
    CHECK(n == 0);
}

/* ------------------------------------------------------------------ *
 * test_list_dir_no_read_perm
 *   Create a directory with 000 perms and verify FS_ERR_PERMISSION.
 *   We skip this test if running as root (root bypasses perms).
 * ------------------------------------------------------------------ */
static void
test_list_dir_no_read_perm(void)
{
    printf("test_list_dir_no_read_perm\n");
#ifdef _WIN32
    /* Windows has no POSIX permission bits, so a directory cannot be made
     * unreadable that way. */
    printf("  (skipped: not supported on Windows)\n");
    return;
#else
    if (t_geteuid() == 0) {
        printf("  (skipped: running as root)\n");
        return;
    }

    char path[256];
    snprintf(path, sizeof(path), "%s/locked", SANDBOX);
    t_mkdir(path);
    chmod(path, 0000);

    DirEntry entries[4];
    int n = (int)(sizeof(entries) / sizeof(entries[0]));
    FSStatus s = fs_list_dir(path, entries, &n);
    CHECK(s == FS_ERR_PERMISSION);
    CHECK(n == 0);

    /* Cleanup: restore perms so rm works. */
    chmod(path, 0700);
#endif
}

/* ------------------------------------------------------------------ *
 * test_list_dir_capacity_limited
 *   Pass a small capacity and verify count is capped.
 * ------------------------------------------------------------------ */
static void
test_list_dir_capacity_limited(void)
{
    printf("test_list_dir_capacity_limited\n");
    DirEntry entries[2];
    int n = 2;
    FSStatus s = fs_list_dir(SANDBOX, entries, &n);
    CHECK(s == FS_OK);
    CHECK(n == 2);
    /* Sorted: fileA.txt, fileB.txt */
    CHECK_STR_EQ(entries[0].meta.name, "fileA.txt");
    CHECK_STR_EQ(entries[1].meta.name, "fileB.txt");
}

/* ------------------------------------------------------------------ *
 * test_list_dir_with_symlink
 * ------------------------------------------------------------------ */
static void
test_list_dir_with_symlink(void)
{
    printf("test_list_dir_with_symlink\n");
    if (!t_can_symlink()) {
        printf("  (skipped: this user may not create symbolic links)\n");
        return;
    }

    char linkpath[256];
    snprintf(linkpath, sizeof(linkpath), "%s/alink", SANDBOX);
    t_unlink(linkpath);
    int rc = t_symlink("fileA.txt", linkpath);
    CHECK(rc == 0);

    DirEntry entries[16];
    int n = (int)(sizeof(entries) / sizeof(entries[0]));
    FSStatus s = fs_list_dir(SANDBOX, entries, &n);
    CHECK(s == FS_OK);

    /* Find alink */
    int found = 0;
    for (int i = 0; i < n; i++) {
        if (strcmp(entries[i].meta.name, "alink") == 0) {
            CHECK(entries[i].meta.type == FS_TYPE_SYMLINK);
            CHECK(strlen(entries[i].meta.link_target) > 0);
            CHECK(entries[i].meta.link_target_exists == 1);
            found = 1;
            break;
        }
    }
    CHECK(found == 1);

    t_unlink(linkpath);
}

/* ------------------------------------------------------------------ *
 * test_list_dir_broken_symlink
 * ------------------------------------------------------------------ */
static void
test_list_dir_broken_symlink(void)
{
    printf("test_list_dir_broken_symlink\n");
    if (!t_can_symlink()) {
        printf("  (skipped: this user may not create symbolic links)\n");
        return;
    }

    char linkpath[256];
    snprintf(linkpath, sizeof(linkpath), "%s/broken", SANDBOX);
    t_unlink(linkpath);
    int rc = t_symlink("/no/such/target/exists/here", linkpath);
    CHECK(rc == 0);

    DirEntry entries[16];
    int n = (int)(sizeof(entries) / sizeof(entries[0]));
    FSStatus s = fs_list_dir(SANDBOX, entries, &n);
    CHECK(s == FS_OK);

    int found = 0;
    for (int i = 0; i < n; i++) {
        if (strcmp(entries[i].meta.name, "broken") == 0) {
            CHECK(entries[i].meta.type == FS_TYPE_SYMLINK);
            CHECK(entries[i].meta.link_target_exists == -1);
            found = 1;
            break;
        }
    }
    CHECK(found == 1);

    t_unlink(linkpath);
}

/* ------------------------------------------------------------------ *
 * test_cwd
 * ------------------------------------------------------------------ */
static void
test_cwd(void)
{
    printf("test_cwd\n");

    char *before = fs_get_cwd();
    CHECK(before != NULL);
    CHECK(strlen(before) > 0);
#ifdef _WIN32
    CHECK(before[1] == ':');   /* "C:\..." */
#else
    CHECK(before[0] == '/');   /* absolute */
#endif

    /* chdir to sandbox then back */
    FSStatus s = fs_set_cwd(SANDBOX);
    CHECK(s == FS_OK);

    char *mid = fs_get_cwd();
    CHECK(mid != NULL);
    /* realpath-normalized, ends with SANDBOX */
    CHECK(strstr(mid, SANDBOX) == mid + strlen(mid) - strlen(SANDBOX));

    s = fs_set_cwd(before);
    CHECK(s == FS_OK);
    free(mid);
    free(before);
}

/* ------------------------------------------------------------------ *
 * test_cwd_errors
 * ------------------------------------------------------------------ */
static void
test_cwd_errors(void)
{
    printf("test_cwd_errors\n");

    FSStatus s = fs_set_cwd("/no/such/dir/anywhere/12345");
    CHECK(s == FS_ERR_NOT_FOUND);

#ifdef _WIN32
    /* A file (not a directory) must be rejected. */
    char file[512];
    snprintf(file, sizeof(file), "%s/fileA.txt", SANDBOX);
    s = fs_set_cwd(file);
#else
    s = fs_set_cwd("/etc/hostname");   /* not a directory */
#endif
    CHECK(s == FS_ERR_NOT_DIR);

    s = fs_set_cwd(NULL);
    CHECK(s == FS_ERR_INVALID_PATH);

    s = fs_set_cwd("");
    CHECK(s == FS_ERR_INVALID_PATH);
}

/* ------------------------------------------------------------------ *
 * main
 * ------------------------------------------------------------------ */
int
main(void)
{
    printf("==== fs_listing + fs_navigation test ====\n");
    t_sandbox_init("listing");

    make_sandbox();
    atexit(rm_sandbox);

    test_list_dir_basic();
    test_list_dir_nonexistent();
    test_list_dir_file_not_dir();
    test_list_dir_no_read_perm();
    test_list_dir_capacity_limited();
    test_list_dir_with_symlink();
    test_list_dir_broken_symlink();
    test_cwd();
    test_cwd_errors();

    printf("==== Result: %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}