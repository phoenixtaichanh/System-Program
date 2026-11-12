/* ============================================================================
 * test_fs_symlink.c -- Tests for the symbolic link module.
 *
 * Covers fs_is_symlink, fs_read_link_target, fs_link_target_exists and
 * fs_create_symlink.
 * ============================================================================
 */

#define _XOPEN_SOURCE 700

#include "test_util.h"
#include "fs_symlink.h"
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

#define CHECK_STR_EQ(actual, expected) do { \
    if (strcmp((actual), (expected)) == 0) { g_pass++; } \
    else { \
        g_fail++; \
        fprintf(stderr, "  FAIL line %d: \"%s\" != \"%s\"\n", \
                __LINE__, (actual), (expected)); \
    } \
} while (0)

#define SANDBOX t_sandbox_path()

static void
rm_sandbox(void)
{
    t_rmtree(SANDBOX);
}

/* ------------------------------------------------------------------ *
 * test_create_and_read
 * ------------------------------------------------------------------ */
static void
test_create_and_read(void)
{
    rm_sandbox();
    t_mkdir(SANDBOX);

    /* The target file. */
    char target[256];
    snprintf(target, sizeof(target), "%s/target.txt", SANDBOX);
    CHECK(t_write_file(target, "data\n") == 0);

    /* The link. */
    char link[256];
    snprintf(link, sizeof(link), "%s/link.txt", SANDBOX);

    FSStatus s = fs_create_symlink(target, link);
    CHECK(s == FS_OK);

    /* The link is a symlink, the target is not. */
    CHECK(fs_is_symlink(link) == 1);
    CHECK(fs_is_symlink(target) == 0);

    /* readlink returns the target. */
    char buf[FS_PATH_MAX];
    CHECK(fs_read_link_target(link, buf, sizeof(buf)) == FS_OK);
    CHECK_STR_EQ(buf, target);

    /* The target exists. */
    CHECK(fs_link_target_exists(link) == 1);
}

/* ------------------------------------------------------------------ *
 * test_broken_link
 * ------------------------------------------------------------------ */
static void
test_broken_link(void)
{
    char link[256];
    snprintf(link, sizeof(link), "%s/broken", SANDBOX);

    FSStatus s = fs_create_symlink("/nonexistent/target/xyz", link);
    CHECK(s == FS_OK);
    CHECK(fs_is_symlink(link) == 1);
    CHECK(fs_link_target_exists(link) == -1);

    char buf[FS_PATH_MAX];
    CHECK(fs_read_link_target(link, buf, sizeof(buf)) == FS_OK);
    CHECK_STR_EQ(buf, "/nonexistent/target/xyz");
}

/* ------------------------------------------------------------------ *
 * test_relative_link
 * ------------------------------------------------------------------ */
static void
test_relative_link(void)
{
    char link[256];
    snprintf(link, sizeof(link), "%s/relative", SANDBOX);

    CHECK(fs_create_symlink("target.txt", link) == FS_OK);

    char buf[FS_PATH_MAX];
    CHECK(fs_read_link_target(link, buf, sizeof(buf)) == FS_OK);
    CHECK_STR_EQ(buf, "target.txt");

    /* A relative link that points at an existing file resolves. */
    CHECK(fs_link_target_exists(link) == 1);
}

/* ------------------------------------------------------------------ *
 * test_errors
 * ------------------------------------------------------------------ */
static void
test_errors(void)
{
    char buf[FS_PATH_MAX];

    /* readlink on a regular file must fail. */
    char target[256];
    snprintf(target, sizeof(target), "%s/target.txt", SANDBOX);
    CHECK(fs_read_link_target(target, buf, sizeof(buf)) != FS_OK);

    /* NULL arguments must be rejected. */
    CHECK(fs_is_symlink(NULL) == 0);
    CHECK(fs_read_link_target(NULL, buf, sizeof(buf)) != FS_OK);
    CHECK(fs_create_symlink(NULL, NULL) != FS_OK);

    /* Creating a link where one already exists must fail. */
    char link[256];
    snprintf(link, sizeof(link), "%s/link.txt", SANDBOX);
    CHECK(fs_create_symlink("/tmp", link) != FS_OK);
}

/* ------------------------------------------------------------------ *
 * main
 * ------------------------------------------------------------------ */
int
main(void)
{
    printf("==== fs_symlink test ====\n");
    t_sandbox_init("symlink");

    /* On Windows creating a symbolic link needs Administrator rights or
     * Developer Mode. Without that privilege the tests are skipped rather
     * than reported as failures. */
    if (!t_can_symlink()) {
        printf("  (skipped: this user may not create symbolic links)\n");
        rm_sandbox();
        printf("==== Result: 0 passed, 0 failed, 1 skipped ====\n");
        return 0;
    }

    test_create_and_read();
    test_broken_link();
    test_relative_link();
    test_errors();

    rm_sandbox();

    printf("==== Result: %d passed, %d failed ====\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
