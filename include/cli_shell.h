/* ============================================================================
 * cli/shell.h -- Interactive command-line shell.
 * ============================================================================
 */

#ifndef CLI_SHELL_H
#define CLI_SHELL_H

#include "fs_types.h"

/* Run the interactive shell.
 *
 * `current_path` is the initial working directory (it is updated in place
 * as the user navigates).
 *
 * Returns FS_OK when the user leaves the shell normally. */
FSStatus cli_shell_run(const char *initial_path);

/* Run a single command line and print the result to stdout.
 * Used for the "--exec" mode of the application. */
FSStatus cli_shell_exec(const char *initial_path, const char *command);

#endif /* CLI_SHELL_H */
