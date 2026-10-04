/* ============================================================================
 * test_utils.c -- Smoke test for path_utils, format_utils, logger.
 *
 * Build:
 *   make test_utils
 *
 * Run:
 *   ./build/test_utils
 *
 * Exit code 0 = all assertions passed.
 * Exit code 1 = at least one assertion failed.
 * ============================================================================
 */

#define _XOPEN_SOURCE 700   /* expose S_IF* macros */
#define _POSIX_C_SOURCE 200809L

#include "path_utils.h"
#include "format_utils.h"
#include "logger.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>

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
 * path_is_absolute
 * ------------------------------------------------------------------ */
static void
test_path_is_absolute(void)
{
    printf("test_path_is_absolute\n");
    CHECK(path_is_absolute("/") == true);
    CHECK(path_is_absolute("/foo") == true);
    CHECK(path_is_absolute("/foo/bar") == true);
    CHECK(path_is_absolute("foo") == false);
    CHECK(path_is_absolute("") == false);
    CHECK(path_is_absolute("./foo") == false);
    CHECK(path_is_absolute("../foo") == false);
    CHECK(path_is_absolute(NULL) == false);
}

/* ------------------------------------------------------------------ *
 * path_join
 * ------------------------------------------------------------------ */
static void
test_path_join(void)
{
    printf("test_path_join\n");
    char buf[FS_PATH_MAX];

    path_join("/a/b", "c", buf, sizeof(buf));
    CHECK_STR_EQ(buf, "/a/b/c");

    path_join("/a/b/", "c", buf, sizeof(buf));
    CHECK_STR_EQ(buf, "/a/b/c");

    path_join("/a/b", "/c", buf, sizeof(buf));
    CHECK_STR_EQ(buf, "/c");                /* absolute leaf wins */

    path_join("", "c", buf, sizeof(buf));
    CHECK_STR_EQ(buf, "c");

    path_join(NULL, "/abs", buf, sizeof(buf));
    CHECK_STR_EQ(buf, "/abs");

    path_join("/a", "", buf, sizeof(buf));
    CHECK_STR_EQ(buf, "/a");                /* empty leaf is OK */
}

/* ------------------------------------------------------------------ *
 * path_basename / path_dirname
 * ------------------------------------------------------------------ */
static void
test_path_basename_dirname(void)
{
    printf("test_path_basename_dirname\n");

    CHECK_STR_EQ(path_basename("/a/b/c"),     "c");
    CHECK_STR_EQ(path_basename("/a/b/"),      "b");
    CHECK_STR_EQ(path_basename("a"),          "a");
    CHECK_STR_EQ(path_basename(""),           "");
    CHECK_STR_EQ(path_basename("/"),          "");

    char d[FS_PATH_MAX];

    path_dirname("/a/b/c", d, sizeof(d));
    CHECK_STR_EQ(d, "/a/b");

    path_dirname("/a", d, sizeof(d));
    CHECK_STR_EQ(d, "/");

    path_dirname("a", d, sizeof(d));
    CHECK_STR_EQ(d, ".");

    path_dirname("", d, sizeof(d));
    CHECK_STR_EQ(d, ".");

    path_dirname("/", d, sizeof(d));
    CHECK_STR_EQ(d, "/");
}

/* ------------------------------------------------------------------ *
 * path_normalize
 * ------------------------------------------------------------------ */
static void
test_path_normalize(void)
{
    printf("test_path_normalize\n");
    char buf[FS_PATH_MAX];

    snprintf(buf, sizeof(buf), "/a/b/c");
    path_normalize(buf, sizeof(buf));
    CHECK_STR_EQ(buf, "/a/b/c");

    snprintf(buf, sizeof(buf), "/a/./b");
    path_normalize(buf, sizeof(buf));
    CHECK_STR_EQ(buf, "/a/b");

    snprintf(buf, sizeof(buf), "/a/b/../c");
    path_normalize(buf, sizeof(buf));
    CHECK_STR_EQ(buf, "/a/c");

    snprintf(buf, sizeof(buf), "//a///b");
    path_normalize(buf, sizeof(buf));
    CHECK_STR_EQ(buf, "/a/b");

    snprintf(buf, sizeof(buf), "a/b");
    path_normalize(buf, sizeof(buf));
    CHECK_STR_EQ(buf, "a/b");

    snprintf(buf, sizeof(buf), "a/../b");
    path_normalize(buf, sizeof(buf));
    CHECK_STR_EQ(buf, "b");

    snprintf(buf, sizeof(buf), "a/../../b");
    path_normalize(buf, sizeof(buf));
    CHECK_STR_EQ(buf, "../b");

    snprintf(buf, sizeof(buf), "..");
    path_normalize(buf, sizeof(buf));
    CHECK_STR_EQ(buf, "..");

    snprintf(buf, sizeof(buf), "%s", "");
    path_normalize(buf, sizeof(buf));
    CHECK_STR_EQ(buf, ".");
}

/* ------------------------------------------------------------------ *
 * fmt_size_human
 * ------------------------------------------------------------------ */
static void
test_fmt_size_human(void)
{
    printf("test_fmt_size_human\n");
    char buf[64];

    fmt_size_human(0,        buf, sizeof(buf)); CHECK_STR_EQ(buf, "0 B");
    fmt_size_human(512,      buf, sizeof(buf)); CHECK_STR_EQ(buf, "512 B");
    fmt_size_human(1023,     buf, sizeof(buf)); CHECK_STR_EQ(buf, "1023 B");
    fmt_size_human(1024,     buf, sizeof(buf)); CHECK_STR_EQ(buf, "1.0 KB");
    fmt_size_human(1536,     buf, sizeof(buf)); CHECK_STR_EQ(buf, "1.5 KB");
    fmt_size_human(1024*1024,   buf, sizeof(buf)); CHECK_STR_EQ(buf, "1.0 MB");
    fmt_size_human(5*1024*1024, buf, sizeof(buf)); CHECK_STR_EQ(buf, "5.0 MB");
}

/* ------------------------------------------------------------------ *
 * fmt_time_local
 * ------------------------------------------------------------------ */
static void
test_fmt_time_local(void)
{
    printf("test_fmt_time_local\n");
    char buf[64];

    /* A specific time: 2026-10-04 15:42:18 UTC = 1799062938. */
    /* We'll just check the function runs and produces a string of length
     * 19. We don't hardcode the time zone-dependent output. */
    bool ok = fmt_time_local((time_t)1799062938, buf, sizeof(buf));
    CHECK(ok == true);
    CHECK(strlen(buf) == 19);
    CHECK(buf[4]  == '-');
    CHECK(buf[7]  == '-');
    CHECK(buf[10] == ' ');
    CHECK(buf[13] == ':');
    CHECK(buf[16] == ':');

    ok = fmt_time_local((time_t)-1, buf, sizeof(buf));
    CHECK(ok == true);
    CHECK_STR_EQ(buf, "(invalid)");
}

/* ------------------------------------------------------------------ *
 * fmt_perm_symbolic / fmt_perm_octal
 * ------------------------------------------------------------------ */
static void
test_fmt_perm(void)
{
    printf("test_fmt_perm\n");
    char buf[16];

    fmt_perm_symbolic(S_IFREG | 0754, buf, sizeof(buf));
    CHECK_STR_EQ(buf, "-rwxr-xr--");

    fmt_perm_symbolic(S_IFDIR | 0750, buf, sizeof(buf));
    CHECK_STR_EQ(buf, "drwxr-x---");

    fmt_perm_symbolic(S_IFLNK | 0777, buf, sizeof(buf));
    CHECK_STR_EQ(buf, "lrwxrwxrwx");

    fmt_perm_octal(0754, buf, sizeof(buf));
    CHECK_STR_EQ(buf, "0754");

    fmt_perm_octal(0644, buf, sizeof(buf));
    CHECK_STR_EQ(buf, "0644");
}

/* ------------------------------------------------------------------ *
 * fmt_status_message
 * ------------------------------------------------------------------ */
static void
test_fmt_status_message(void)
{
    printf("test_fmt_status_message\n");
    CHECK_STR_EQ(fmt_status_message(FS_OK, 0), "OK");
    CHECK_STR_EQ(fmt_status_message(FS_ERR_NOT_FOUND, 0), "Path not found");
    CHECK_STR_EQ(fmt_status_message(FS_ERR_PERMISSION, EACCES), "Permission denied");
    CHECK_STR_EQ(fmt_status_message(FS_ERR_NOT_DIR, 0), "Not a directory");
    CHECK_STR_EQ(fmt_status_message(FS_ERR_NOMEM, 0), "Out of memory");
}

/* ------------------------------------------------------------------ *
 * fmt_type_label
 * ------------------------------------------------------------------ */
static void
test_fmt_type_label(void)
{
    printf("test_fmt_type_label\n");
    CHECK_STR_EQ(fmt_type_label(FS_TYPE_REGULAR),   "Regular file");
    CHECK_STR_EQ(fmt_type_label(FS_TYPE_DIRECTORY), "Directory");
    CHECK_STR_EQ(fmt_type_label(FS_TYPE_SYMLINK),   "Symbolic link");
}

/* ------------------------------------------------------------------ *
 * logger
 * ------------------------------------------------------------------ */
static void
test_logger(void)
{
    printf("test_logger\n");
    char buf[4096];

    logger_init();
    logger_append("first line");
    logger_append("second line");
    logger_append("third line");

    size_t n = logger_recent(buf, sizeof(buf), 10);
    CHECK(n > 0);
    CHECK(strstr(buf, "first line")  != NULL);
    CHECK(strstr(buf, "second line") != NULL);
    CHECK(strstr(buf, "third line")  != NULL);
    /* Most recent is on the last line. */
    char *last_nl = strrchr(buf, '\n');
    CHECK(last_nl != NULL);
    CHECK(strstr(last_nl, "third line") != NULL);

    /* Cap at max=2 -> only the 2 most recent. */
    n = logger_recent(buf, sizeof(buf), 2);
    CHECK(strstr(buf, "first line")  == NULL);
    CHECK(strstr(buf, "second line") != NULL);
    CHECK(strstr(buf, "third line")  != NULL);

    /* Clear. */
    logger_clear();
    n = logger_recent(buf, sizeof(buf), 10);
    CHECK(n == 0);
}

/* ------------------------------------------------------------------ *
 * main
 * ------------------------------------------------------------------ */
int
main(void)
{
    printf("==== Utils smoke test ====\n");

    test_path_is_absolute();
    test_path_join();
    test_path_basename_dirname();
    test_path_normalize();
    test_fmt_size_human();
    test_fmt_time_local();
    test_fmt_perm();
    test_fmt_status_message();
    test_fmt_type_label();
    test_logger();

    printf("==== Result: %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}