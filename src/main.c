/* ============================================================================
 * main.c -- Application entry point.
 *
 * Two modes:
 *   - GUI  (default)      : a GTK 3 window with a directory tree, a file
 *                           list, a metadata panel and an activity log.
 *   - CLI  (--cli)        : the interactive command shell.
 *   - One-shot (--exec)   : run a single command and exit.
 * ============================================================================
 */

#include <gtk/gtk.h>

#include "cli_shell.h"
#include "fs_navigation.h"
#include "gui_main_window.h"
#include "logger.h"
#include "path_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ *
 * GUI activation
 * ------------------------------------------------------------------ */
static void
on_activate(GtkApplication *app, gpointer user_data)
{
    const char *start_path = (const char *)user_data;

    GtkWidget *window = gui_main_window_new(start_path);
    if (window == NULL) return;

    gtk_window_set_application(GTK_WINDOW(window), app);
    gtk_widget_show_all(window);
}

/* ------------------------------------------------------------------ *
 * Print the usage banner.
 * ------------------------------------------------------------------ */
static void
print_usage(const char *argv0)
{
    printf("File System Explorer & Metadata Analyzer\n\n");
    printf("Usage: %s [options]\n\n", argv0);
    printf("Options:\n");
    printf("  (no option)      start the graphical interface\n");
    printf("  --cli            start the interactive command shell\n");
    printf("  --exec <cmd>     run a single command and exit\n");
    printf("  --path <dir>     set the starting directory\n");
    printf("  --version        print the version and exit\n");
    printf("  --help           print this help and exit\n");
}

/* ------------------------------------------------------------------ *
 * main
 * ------------------------------------------------------------------ */
int
main(int argc, char *argv[])
{
    const char *cli_mode   = NULL;
    const char *exec_cmd  = NULL;
    const char *start_dir = NULL;
    int         show_help = 0;
    int         show_version = 0;

    /* GTK parses the remaining argv itself, so our own options have to be
     * removed from it. The leftovers (e.g. "--display=:99") are kept. */
    int  rest_argc = 1;
    char **rest_argv = (char **)calloc((size_t)argc + 1, sizeof(char *));
    if (rest_argv == NULL) {
        fprintf(stderr, "fs_explorer: out of memory\n");
        return 1;
    }
    rest_argv[0] = argv[0];

    /* Parse our own options before handing the rest to GTK. */
    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "--cli") == 0) {
            cli_mode = "--cli";
            i++;
        } else if (strcmp(argv[i], "--exec") == 0 && i + 1 < argc) {
            exec_cmd = argv[i + 1];
            i += 2;
        } else if (strcmp(argv[i], "--path") == 0 && i + 1 < argc) {
            start_dir = argv[i + 1];
            i += 2;
        } else if (strcmp(argv[i], "--help") == 0 ||
                   strcmp(argv[i], "-h") == 0) {
            show_help = 1;
            i++;
        } else if (strcmp(argv[i], "--version") == 0) {
            show_version = 1;
            i++;
        } else {
            /* Not ours: keep it for GTK (or ignore it). */
            rest_argv[rest_argc++] = argv[i];
            i++;
        }
    }
    rest_argv[rest_argc] = NULL;

    if (show_help) {
        print_usage(argv[0]);
        free(rest_argv);
        return 0;
    }
    if (show_version) {
        printf("File System Explorer & Metadata Analyzer 1.0.0\n");
        free(rest_argv);
        return 0;
    }

    /* Initialize the activity log. */
    logger_init();

    /* Work out the starting directory. */
    char cwd[FS_PATH_MAX];
    if (start_dir != NULL && start_dir[0] != '\0') {
        snprintf(cwd, sizeof(cwd), "%s", start_dir);
    } else {
        char *p = fs_get_cwd();
        if (p != NULL) {
            snprintf(cwd, sizeof(cwd), "%s", p);
            free(p);
        } else {
            snprintf(cwd, sizeof(cwd), "%s", path_home_dir());
        }
    }

    /* ---- CLI modes ------------------------------------------------- */
    if (exec_cmd != NULL) {
        int rc = (cli_shell_exec(cwd, exec_cmd) == FS_OK) ? 0 : 1;
        free(rest_argv);
        return rc;
    }

    if (cli_mode != NULL) {
        cli_shell_run(cwd);
        free(rest_argv);
        return 0;
    }

    /* ---- GUI mode -------------------------------------------------- */
    GtkApplication *app;
    int status;

    app = gtk_application_new(
        "vn.vku.fs_explorer",                /* Application ID */
        G_APPLICATION_DEFAULT_FLAGS);        /* non-deprecated */

    g_signal_connect(app, "activate", G_CALLBACK(on_activate),
                     (gpointer)start_dir);

    status = g_application_run(G_APPLICATION(app), rest_argc, rest_argv);

    g_object_unref(app);
    free(rest_argv);
    return status;
}
