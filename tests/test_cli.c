/* ============================================================================
 * test_cli.c -- Tests for the command interpreter.
 *
 * The interpreter writes its output through a callback, so the tests can
 * capture it and assert on the result.
 * ============================================================================
 */

#define _XOPEN_SOURCE 700

#include "cli_command.h"
#include "fs_types.h"
#include "test_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond) do { \
    if (cond) { g_pass++; } \
    else { \
        g_fail++; \
        fprintf(stderr, "  FAIL line %d: %s\n", __LINE__, #cond); \
    } \
} while (0)

/* ------------------------------------------------------------------ *
 * Captured output buffer
 * ------------------------------------------------------------------ */
#define CAP_SIZE 65536

typedef struct {
    char  buf[CAP_SIZE];
    size_t len;
} Capture;

static void
capture_writer(const char *line, void *user_data)
{
    Capture *c = (Capture *)user_data;
    if (c == NULL) return;

    size_t n = strlen(line);
    if (c->len + n + 1 >= CAP_SIZE) return;

    memcpy(c->buf + c->len, line, n + 1);
    c->len += n;
}

static void
capture_reset(Capture *c)
{
    c->buf[0] = '\0';
    c->len = 0;
}

static int
capture_contains(const Capture *c, const char *needle)
{
    return strstr(c->buf, needle) != NULL;
}

#define SANDBOX t_sandbox_path()

static void
rm_sandbox(void)
{
    t_rmtree(SANDBOX);
}

/* ------------------------------------------------------------------ *
 * test_help
 * ------------------------------------------------------------------ */
static void
test_help(void)
{
    Capture cap;
    capture_reset(&cap);

    cli_print_help(capture_writer, &cap);

    CHECK(cap.len > 0);
    CHECK(capture_contains(&cap, "ls"));
    CHECK(capture_contains(&cap, "cd"));
    CHECK(capture_contains(&cap, "pwd"));
    CHECK(capture_contains(&cap, "stat"));
    CHECK(capture_contains(&cap, "mkdir"));
    CHECK(capture_contains(&cap, "find"));
}

/* ------------------------------------------------------------------ *
 * test_pwd_and_cd
 * ------------------------------------------------------------------ */
static void
test_pwd_and_cd(void)
{
    Capture cap;
    char cwd[FS_PATH_MAX];
    snprintf(cwd, sizeof(cwd), "%s", SANDBOX);

    rm_sandbox();
    t_mkdir(SANDBOX);

    capture_reset(&cap);
    CHECK(cli_run_line_with_state("pwd", cwd, sizeof(cwd),
                                  capture_writer, &cap) == FS_OK);
    CHECK(capture_contains(&cap, SANDBOX));

    /* cd into the sandbox, then pwd must report it. */
    capture_reset(&cap);
    CHECK(cli_run_line_with_state("cd /tmp", cwd, sizeof(cwd),
                                  capture_writer, &cap) == FS_OK);
    CHECK(strcmp(cwd, "/tmp") == 0);

    /* cd into a directory that does not exist must fail. */
    capture_reset(&cap);
    CHECK(cli_run_line_with_state("cd /nonexistent/xyz", cwd, sizeof(cwd),
                                  capture_writer, &cap) != FS_OK);
    CHECK(capture_contains(&cap, "cd:"));
}

/* ------------------------------------------------------------------ *
 * test_ls
 * ------------------------------------------------------------------ */
static void
test_ls(void)
{
    rm_sandbox();
    t_mkdir(SANDBOX);

    char path[512];
    snprintf(path, sizeof(path), "%s/one.txt", SANDBOX);
    t_write_file(path, "x\n");

    snprintf(path, sizeof(path), "%s/two.txt", SANDBOX);
    t_write_file(path, "y\n");

    Capture cap;
    char cwd[FS_PATH_MAX];
    snprintf(cwd, sizeof(cwd), "%s", SANDBOX);

    capture_reset(&cap);
    CHECK(cli_run_line_with_state("ls", cwd, sizeof(cwd),
                                  capture_writer, &cap) == FS_OK);
    CHECK(capture_contains(&cap, "one.txt"));
    CHECK(capture_contains(&cap, "two.txt"));
    CHECK(capture_contains(&cap, "2 items"));

    /* ls of a missing directory must report an error. */
    capture_reset(&cap);
    CHECK(cli_run_line_with_state("ls /nonexistent/xyz", cwd, sizeof(cwd),
                                  capture_writer, &cap) != FS_OK);
    CHECK(capture_contains(&cap, "ls:"));
}

/* ------------------------------------------------------------------ *
 * test_stat
 * ------------------------------------------------------------------ */
static void
test_stat(void)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/stat.txt", SANDBOX);

    CHECK(t_write_file(path, "0123456789\n") == 0);

    Capture cap;
    char cwd[FS_PATH_MAX];
    snprintf(cwd, sizeof(cwd), "%s", SANDBOX);

    capture_reset(&cap);
    CHECK(cli_run_line_with_state("stat stat.txt", cwd, sizeof(cwd),
                                  capture_writer, &cap) == FS_OK);
    CHECK(capture_contains(&cap, "stat.txt"));
    CHECK(capture_contains(&cap, "Size"));
    CHECK(capture_contains(&cap, "Owner"));
    CHECK(capture_contains(&cap, "Mode"));

    /* stat without an operand must fail. */
    capture_reset(&cap);
    CHECK(cli_run_line_with_state("stat", cwd, sizeof(cwd),
                                  capture_writer, &cap) != FS_OK);
}

/* ------------------------------------------------------------------ *
 * test_mutation
 * ------------------------------------------------------------------ */
static void
test_mutation(void)
{
    rm_sandbox();
    t_mkdir(SANDBOX);

    Capture cap;
    char cwd[FS_PATH_MAX];
    snprintf(cwd, sizeof(cwd), "%s", SANDBOX);

    /* mkdir */
    capture_reset(&cap);
    CHECK(cli_run_line_with_state("mkdir newdir", cwd, sizeof(cwd),
                                  capture_writer, &cap) == FS_OK);
    snprintf(cwd, sizeof(cwd), "%s", SANDBOX);
    CHECK(t_is_dir(t_sandbox_sub("newdir")));

    /* touch */
    capture_reset(&cap);
    CHECK(cli_run_line_with_state("touch file.txt", cwd, sizeof(cwd),
                                  capture_writer, &cap) == FS_OK);
    CHECK(t_is_reg(t_sandbox_sub("file.txt")));

    /* mv */
    capture_reset(&cap);
    CHECK(cli_run_line_with_state("mv file.txt renamed.txt", cwd,
                                  sizeof(cwd), capture_writer, &cap) == FS_OK);
    CHECK(t_exists(t_sandbox_sub("renamed.txt")));
    CHECK(!t_exists(t_sandbox_sub("file.txt")));

    /* rm */
    capture_reset(&cap);
    CHECK(cli_run_line_with_state("rm renamed.txt", cwd, sizeof(cwd),
                                  capture_writer, &cap) == FS_OK);
    CHECK(!t_exists(t_sandbox_sub("renamed.txt")));

    /* rmdir on a directory that still has content must fail. */
    CHECK(t_write_file(t_sandbox_sub("newdir/inner.txt"), "x\n") == 0);

    capture_reset(&cap);
    CHECK(cli_run_line_with_state("rmdir newdir", cwd, sizeof(cwd),
                                  capture_writer, &cap) != FS_OK);
    CHECK(capture_contains(&cap, "rmdir:"));

    /* After emptying the directory, rmdir must succeed. */
    t_unlink(t_sandbox_sub("newdir/inner.txt"));
    capture_reset(&cap);
    CHECK(cli_run_line_with_state("rmdir newdir", cwd, sizeof(cwd),
                                  capture_writer, &cap) == FS_OK);
    CHECK(capture_contains(&cap, "removed directory"));

    /* rmdir on a regular file must fail. */
    capture_reset(&cap);
    CHECK(cli_run_line_with_state("touch a_file.txt", cwd, sizeof(cwd),
                                  capture_writer, &cap) == FS_OK);
    capture_reset(&cap);
    CHECK(cli_run_line_with_state("rmdir a_file.txt", cwd, sizeof(cwd),
                                  capture_writer, &cap) != FS_OK);
    CHECK(capture_contains(&cap, "Not a directory"));
}

/* ------------------------------------------------------------------ *
 * test_symlink
 * ------------------------------------------------------------------ */
static void
test_symlink(void)
{
    /* On Windows creating a symbolic link needs Administrator rights or
     * Developer Mode; without that privilege the case is skipped. */
    if (!t_can_symlink()) {
        printf("  (skipped: this user may not create symbolic links)\n");
        return;
    }

    rm_sandbox();
    t_mkdir(SANDBOX);

    char target[512], link[512];
    snprintf(target, sizeof(target), "%s/target.txt", SANDBOX);
    snprintf(link, sizeof(link), "%s/the_link", SANDBOX);

    t_write_file(target, "x\n");

    Capture cap;
    char cwd[FS_PATH_MAX];
    snprintf(cwd, sizeof(cwd), "%s", SANDBOX);

    capture_reset(&cap);
    CHECK(cli_run_line_with_state("ln -s target.txt the_link", cwd,
                                  sizeof(cwd), capture_writer, &cap) == FS_OK);

    CHECK(t_is_symlink(link));

    /* readlink must print the target. */
    capture_reset(&cap);
    CHECK(cli_run_line_with_state("readlink the_link", cwd, sizeof(cwd),
                                  capture_writer, &cap) == FS_OK);
    CHECK(capture_contains(&cap, "target.txt"));

    /* ln without -s must fail. */
    capture_reset(&cap);
    CHECK(cli_run_line_with_state("ln a b", cwd, sizeof(cwd),
                                  capture_writer, &cap) != FS_OK);
}

/* ------------------------------------------------------------------ *
 * test_find_and_du_and_df
 * ------------------------------------------------------------------ */
static void
test_find_and_du_and_df(void)
{
    rm_sandbox();
    t_mkdir(SANDBOX);

    char path[512];
    snprintf(path, sizeof(path), "%s/found.c", SANDBOX);
    t_write_file(path, "x\n");

    Capture cap;
    char cwd[FS_PATH_MAX];
    snprintf(cwd, sizeof(cwd), "%s", SANDBOX);

    /* find */
    capture_reset(&cap);
    CHECK(cli_run_line_with_state("find *.c .", cwd, sizeof(cwd),
                                  capture_writer, &cap) == FS_OK);
    CHECK(capture_contains(&cap, "found.c"));

    /* du */
    capture_reset(&cap);
    CHECK(cli_run_line_with_state("du .", cwd, sizeof(cwd),
                                  capture_writer, &cap) == FS_OK);
    CHECK(capture_contains(&cap, "Total size"));
    CHECK(capture_contains(&cap, "Files"));

    /* df */
    capture_reset(&cap);
    CHECK(cli_run_line_with_state("df .", cwd, sizeof(cwd),
                                  capture_writer, &cap) == FS_OK);
    CHECK(capture_contains(&cap, "Total"));
}

/* ------------------------------------------------------------------ *
 * test_unknown_command
 * ------------------------------------------------------------------ */
static void
test_unknown_command(void)
{
    Capture cap;
    char cwd[FS_PATH_MAX];
    snprintf(cwd, sizeof(cwd), "%s", SANDBOX);

    capture_reset(&cap);
    CHECK(cli_run_line_with_state("frobnicate", cwd, sizeof(cwd),
                                  capture_writer, &cap) != FS_OK);
    CHECK(capture_contains(&cap, "unknown command"));

    /* An empty line is a no-op. */
    capture_reset(&cap);
    CHECK(cli_run_line_with_state("", cwd, sizeof(cwd),
                                  capture_writer, &cap) == FS_OK);
    CHECK(cap.len == 0);

    /* Whitespace only is also a no-op. */
    capture_reset(&cap);
    CHECK(cli_run_line_with_state("   \t ", cwd, sizeof(cwd),
                                  capture_writer, &cap) == FS_OK);
    CHECK(cap.len == 0);
}

/* ------------------------------------------------------------------ *
 * test_quoted_argument
 * ------------------------------------------------------------------ */
static void
test_quoted_argument(void)
{
    rm_sandbox();
    t_mkdir(SANDBOX);

    char dir[512];
    snprintf(dir, sizeof(dir), "%s/with space", SANDBOX);
    t_mkdir(dir);

    /* Put a file inside so the listing is not empty. */
    char inner[600];
    snprintf(inner, sizeof(inner), "%s/with space/file.txt", SANDBOX);
    CHECK(t_write_file(inner, "x\n") == 0);

    Capture cap;
    char cwd[FS_PATH_MAX];
    snprintf(cwd, sizeof(cwd), "%s", SANDBOX);

    /* The quoted path must be handled as one argument. */
    capture_reset(&cap);
    CHECK(cli_run_line_with_state("ls \"with space\"", cwd, sizeof(cwd),
                                  capture_writer, &cap) == FS_OK);
    CHECK(capture_contains(&cap, "file.txt"));

    /* Command chaining with "&&" must work. */
    char chain[FS_PATH_MAX * 2];
    snprintf(chain, sizeof(chain), "cd \"%s\" && ls", SANDBOX);
    capture_reset(&cap);
    CHECK(cli_run_line_with_state(chain, cwd,
                                  sizeof(cwd), capture_writer, &cap) == FS_OK);
    CHECK(capture_contains(&cap, "with space"));

    /* A failing command must stop an "&&" chain. */
    capture_reset(&cap);
    CHECK(cli_run_line_with_state("cd /nonexistent/xyz && ls", cwd,
                                  sizeof(cwd), capture_writer, &cap) != FS_OK);
    CHECK(capture_contains(&cap, "cd:"));
    CHECK(!capture_contains(&cap, "NAME"));
}

/* ------------------------------------------------------------------ *
 * main
 * ------------------------------------------------------------------ */
int
main(void)
{
    printf("==== cli command test ====\n");
    t_sandbox_init("cli");

    test_help();
    test_pwd_and_cd();
    test_ls();
    test_stat();
    test_mutation();
    test_symlink();
    test_find_and_du_and_df();
    test_unknown_command();
    test_quoted_argument();

    rm_sandbox();

    printf("==== Result: %d passed, %d failed ====\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
