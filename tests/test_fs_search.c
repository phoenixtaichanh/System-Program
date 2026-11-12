/* ============================================================================
 * test_fs_search.c -- Tests for the recursive search module.
 * ============================================================================
 */

#define _XOPEN_SOURCE 700

#include "test_util.h"
#include "fs_search.h"
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

static void
make_file(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    t_write_file(path, "x\n");
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
 *     alpha.txt
 *     beta.log
 *     sub/
 *       gamma.txt
 *       deep/
 *         alpha.txt
 * ------------------------------------------------------------------ */
static void
test_setup(void)
{
    rm_sandbox();
    t_mkdir(SANDBOX);

    make_file(SANDBOX, "alpha.txt");
    make_file(SANDBOX, "beta.log");
    make_dir(SANDBOX, "sub");
    make_file(SANDBOX, "sub/gamma.txt");
    make_dir(SANDBOX, "sub/deep");
    make_file(SANDBOX, "sub/deep/alpha.txt");
}

/* ------------------------------------------------------------------ *
 * test_wildcard
 * ------------------------------------------------------------------ */
static void
test_wildcard(void)
{
    SearchResult results[64];
    int count = 64;

    FSStatus s = fs_search(SANDBOX, "*.txt", 0, -1, 64, results, &count);
    CHECK(s == FS_OK);
    /* alpha.txt, sub/gamma.txt, sub/deep/alpha.txt */
    CHECK(count == 3);

    /* Every match must end with .txt. */
    int all_txt = 1;
    for (int i = 0; i < count; i++) {
        const char *name = results[i].meta.name;
        size_t len = strlen(name);
        if (len < 4 || strcmp(name + len - 4, ".txt") != 0) all_txt = 0;
    }
    CHECK(all_txt == 1);
}

/* ------------------------------------------------------------------ *
 * test_literal
 * ------------------------------------------------------------------ */
static void
test_literal(void)
{
    SearchResult results[64];
    int count = 64;

    /* A literal "alpha" matches both alpha.txt files. */
    FSStatus s = fs_search(SANDBOX, "alpha", 1, -1, 64, results, &count);
    CHECK(s == FS_OK);
    CHECK(count == 2);
}

/* ------------------------------------------------------------------ *
 * test_exact_name
 * ------------------------------------------------------------------ */
static void
test_exact_name(void)
{
    SearchResult results[64];
    int count = 64;

    FSStatus s = fs_search(SANDBOX, "beta.log", 0, -1, 64, results, &count);
    CHECK(s == FS_OK);
    CHECK(count == 1);
    CHECK(strcmp(results[0].meta.name, "beta.log") == 0);
}

/* ------------------------------------------------------------------ *
 * test_depth_limit
 * ------------------------------------------------------------------ */
static void
test_depth_limit(void)
{
    SearchResult results[64];
    int count = 64;

    /* Depth 0 only looks at the root's own entries: alpha.txt matches. */
    FSStatus s = fs_search(SANDBOX, "*.txt", 0, 0, 64, results, &count);
    CHECK(s == FS_OK);
    CHECK(count == 1);
    CHECK(strcmp(results[0].meta.name, "alpha.txt") == 0);

    /* Depth 1 also reaches into the sub-directories. */
    count = 64;
    s = fs_search(SANDBOX, "*.txt", 0, 1, 64, results, &count);
    CHECK(s == FS_OK);
    CHECK(count == 2);

    /* Unlimited depth finds everything. */
    count = 64;
    s = fs_search(SANDBOX, "*.txt", 0, -1, 64, results, &count);
    CHECK(s == FS_OK);
    CHECK(count == 3);
}

/* ------------------------------------------------------------------ *
 * test_max_results
 * ------------------------------------------------------------------ */
static void
test_max_results(void)
{
    SearchResult results[64];
    int count = 64;

    /* The result array is capped: only 2 entries are written. */
    FSStatus s = fs_search(SANDBOX, "*.txt", 0, -1, 2, results, &count);
    CHECK(s == FS_OK);
    CHECK(count <= 2);
}

/* ------------------------------------------------------------------ *
 * test_no_match
 * ------------------------------------------------------------------ */
static void
test_no_match(void)
{
    SearchResult results[64];
    int count = 64;

    FSStatus s = fs_search(SANDBOX, "*.nonexistent", 0, -1, 64,
                            results, &count);
    CHECK(s == FS_OK);
    CHECK(count == 0);
}

/* ------------------------------------------------------------------ *
 * test_errors
 * ------------------------------------------------------------------ */
static void
test_errors(void)
{
    SearchResult results[64];
    int count = 64;

    CHECK(fs_search(NULL, "*.txt", 0, -1, 64, results, &count) != FS_OK);
    CHECK(fs_search(SANDBOX, NULL, 0, -1, 64, results, &count) != FS_OK);
    CHECK(fs_search("/nonexistent/dir", "*.txt", 0, -1, 64,
                    results, &count) != FS_OK);
}

/* ------------------------------------------------------------------ *
 * main
 * ------------------------------------------------------------------ */
int
main(void)
{
    printf("==== fs_search test ====\n");
    t_sandbox_init("search");

    test_setup();

    test_wildcard();
    test_literal();
    test_exact_name();
    test_depth_limit();
    test_max_results();
    test_no_match();
    test_errors();

    rm_sandbox();

    printf("==== Result: %d passed, %d failed ====\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
