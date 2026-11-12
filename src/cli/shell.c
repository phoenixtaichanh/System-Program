/* ============================================================================
 * cli/shell.c -- Interactive command-line shell.
 *
 * The shell is a thin wrapper around the shared command interpreter in
 * cli/command.c. It owns nothing but the prompt and the read loop, which
 * keeps the interpreter reusable and testable.
 * ============================================================================
 */

#include "cli_shell.h"

#include "cli_command.h"
#include "logger.h"
#include "path_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if FS_PLATFORM_WINDOWS
#  include <io.h>
#  define isatty _isatty
#  define fileno _fileno
#endif

/* ------------------------------------------------------------------ *
 * Writer callback that prints to stdout.
 * ------------------------------------------------------------------ */
static void
stdout_writer(const char *line, void *user_data)
{
    (void)user_data;
    fputs(line, stdout);
    fflush(stdout);
}

/* ------------------------------------------------------------------ *
 * Build the prompt, showing the current directory.
 * ------------------------------------------------------------------ */
static void
print_prompt(const char *current_path)
{
    char home[FS_PATH_MAX];
    snprintf(home, sizeof(home), "%s", path_home_dir());

    /* Show "~" instead of the full home directory. */
    const char *display = current_path;
    size_t hlen = strlen(home);
    if (hlen > 1 && strncmp(current_path, home, hlen) == 0 &&
        (current_path[hlen] == '/' || current_path[hlen] == '\0')) {
        static char buf[FS_PATH_MAX];
        snprintf(buf, sizeof(buf), "~%s", current_path + hlen);
        display = buf;
    }

    printf("%s $ ", display);
    fflush(stdout);
}

/* ------------------------------------------------------------------ *
 * cli_shell_run
 * ------------------------------------------------------------------ */
FSStatus
cli_shell_run(const char *initial_path)
{
    char current[FS_PATH_MAX];
    snprintf(current, sizeof(current), "%s",
             (initial_path && initial_path[0]) ? initial_path
                                              : path_home_dir());

    char line[4096];

    printf("File System Explorer & Metadata Analyzer - interactive shell\n");
    printf("Type 'help' for the command list, 'exit' to quit.\n\n");

    for (;;) {
        print_prompt(current);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            /* EOF (Ctrl-D) or a closed stdin. */
            printf("\n");
            break;
        }

        /* Strip the trailing newline. */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }

        /* Skip blank lines. */
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') continue;

        /* "exit" / "quit" leave the shell. */
        if (strcmp(p, "exit") == 0 || strcmp(p, "quit") == 0) {
            printf("Goodbye.\n");
            break;
        }

        cli_run_line_with_state(p, current, sizeof(current),
                                 stdout_writer, NULL);
    }

    return FS_OK;
}

/* ------------------------------------------------------------------ *
 * cli_shell_exec
 * ------------------------------------------------------------------ */
FSStatus
cli_shell_exec(const char *initial_path, const char *command)
{
    char current[FS_PATH_MAX];
    snprintf(current, sizeof(current), "%s",
             (initial_path && initial_path[0]) ? initial_path
                                              : path_home_dir());

    if (command == NULL) return FS_ERR_INVALID_PATH;

    return cli_run_line_with_state(command, current, sizeof(current),
                                   stdout_writer, NULL);
}
