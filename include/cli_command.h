/* ============================================================================
 * cli_command.h -- Shared command interpreter.
 * Implementation lands in PHASE 12.
 * ============================================================================
 */

#ifndef CLI_COMMAND_H
#define CLI_COMMAND_H

#include "fs_types.h"
#include <stddef.h>

typedef void (*cli_writer_fn)(const char *line, void *user_data);

/* Execute one line of input. Output is written through `out`. */
FSStatus cli_run_line(const char *line,
                      cli_writer_fn out,
                      void *user_data);

/* Same as cli_run_line, but takes the current path as an in/out parameter
 * so the CLI can persist state (used by 'cd'). */
FSStatus cli_run_line_with_state(const char *line,
                                 char *current_path,
                                 size_t current_path_size,
                                 cli_writer_fn out,
                                 void *user_data);

/* Print the list of available commands. */
void cli_print_help(cli_writer_fn out, void *user_data);

#endif /* CLI_COMMAND_H */