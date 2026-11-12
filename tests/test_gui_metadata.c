/* ============================================================================
 * test_gui_metadata.c -- Tests for the GTK metadata panel.
 *
 * These tests need a GTK display. They are skipped automatically when no
 * display is available (for example on a headless build machine), so the
 * suite still passes everywhere.
 * ============================================================================
 */

#define _XOPEN_SOURCE 700

#include "test_util.h"
#include "app_state.h"
#include "gui_metadata_panel.h"
#include "fs_metadata.h"
#include "fs_types.h"

#include <gtk/gtk.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int g_pass = 0;
static int g_fail = 0;
static int g_skip = 0;

#define CHECK(cond) do { \
    if (cond) { g_pass++; } \
    else { \
        g_fail++; \
        fprintf(stderr, "  FAIL line %d: %s\n", __LINE__, #cond); \
    } \
} while (0)

#define SANDBOX t_sandbox_path()

/* ------------------------------------------------------------------ *
 * Sandbox helpers
 * ------------------------------------------------------------------ */
static void
rm_sandbox(void)
{
    t_rmtree(SANDBOX);
}

static void
make_sandbox(void)
{
    rm_sandbox();
    t_mkdir(SANDBOX);

    char path[512];
    snprintf(path, sizeof(path), "%s/report.txt", SANDBOX);
    CHECK(t_write_file(path, "hello world\n") == 0);

    snprintf(path, sizeof(path), "%s/subdir", SANDBOX);
    t_mkdir(path);

    snprintf(path, sizeof(path), "%s/link.txt", SANDBOX);
    if (t_symlink("report.txt", path) != 0) { /* ignore */ }
}

/* ------------------------------------------------------------------ *
 * row_text
 *
 * Read a row of the panel and compare it with the expected substring.
 * ------------------------------------------------------------------ */
static void
row_contains(GtkWidget *panel, int row, const char *needle)
{
    char *text = gui_metadata_panel_get_row_text(panel, row);
    if (text == NULL) {
        g_fail++;
        fprintf(stderr, "  FAIL: row %d has no text (expected '%s')\n",
                row, needle);
        return;
    }
    if (strstr(text, needle) == NULL) {
        g_fail++;
        fprintf(stderr, "  FAIL: row %d = '%s', expected to contain '%s'\n",
                row, text, needle);
    } else {
        g_pass++;
    }
    g_free(text);
}

static void
row_equals(GtkWidget *panel, int row, const char *expected)
{
    char *text = gui_metadata_panel_get_row_text(panel, row);
    if (text == NULL) {
        g_fail++;
        fprintf(stderr, "  FAIL: row %d has no text (expected '%s')\n",
                row, expected);
        return;
    }
    if (strcmp(text, expected) != 0) {
        g_fail++;
        fprintf(stderr, "  FAIL: row %d = '%s', expected '%s'\n",
                row, text, expected);
    } else {
        g_pass++;
    }
    g_free(text);
}

/* ------------------------------------------------------------------ *
 * test_empty_panel
 * ------------------------------------------------------------------ */
static void
test_empty_panel(void)
{
    AppState *state = app_state_new();
    GtkWidget *panel = gui_metadata_panel_new(state);

    CHECK(panel != NULL);

    /* With nothing selected the panel says so. */
    row_equals(panel, 0, "No selection");
    row_equals(panel, 1, "-");

    g_object_unref(state);
}

/* ------------------------------------------------------------------ *
 * test_regular_file
 * ------------------------------------------------------------------ */
static void
test_regular_file(void)
{
    make_sandbox();

    AppState *state = app_state_new();
    GtkWidget *panel = gui_metadata_panel_new(state);

    char path[512];
    snprintf(path, sizeof(path), "%s/report.txt", SANDBOX);

    FileMetadata meta;
    CHECK(fs_get_metadata(path, 1, &meta) == FS_OK);

    state->selected = meta;
    g_signal_emit_by_name(state, APP_STATE_SIGNAL_SELECTION_CHANGED);

    row_contains(panel, 0, "report.txt");
    row_contains(panel, 1, "report.txt");
    row_contains(panel, 2, "Regular file");
    row_contains(panel, 3, "12 bytes");
    row_contains(panel, 4, "-rw-");     /* symbolic mode  */
#ifdef _WIN32
    /* Windows only models the read-only flag, so the platform layer
     * synthesizes "rw-rw-rw-" for a writable file. */
    row_contains(panel, 4, "0666");
#else
    row_contains(panel, 4, "0644");     /* octal mode     */
#endif
    row_contains(panel, 9, "inode");
    row_contains(panel, 10, "-");       /* not a symlink  */
    row_contains(panel, 11, "you are:");

    g_object_unref(state);
}

/* ------------------------------------------------------------------ *
 * test_directory
 * ------------------------------------------------------------------ */
static void
test_directory(void)
{
    make_sandbox();

    AppState *state = app_state_new();
    GtkWidget *panel = gui_metadata_panel_new(state);

    char path[512];
    snprintf(path, sizeof(path), "%s/subdir", SANDBOX);

    FileMetadata meta;
    CHECK(fs_get_metadata(path, 0, &meta) == FS_OK);

    state->selected = meta;
    g_signal_emit_by_name(state, APP_STATE_SIGNAL_SELECTION_CHANGED);

    row_contains(panel, 2, "Directory");
    row_equals(panel, 3, "directory");
    row_contains(panel, 4, "d");

    g_object_unref(state);
}

/* ------------------------------------------------------------------ *
 * test_symlink
 * ------------------------------------------------------------------ */
static void
test_symlink(void)
{
    /* On Windows a symbolic link needs Administrator rights or Developer
     * Mode; without that privilege the case is skipped. */
    if (!t_can_symlink()) {
        printf("  (skipped: this user may not create symbolic links)\n");
        g_skip++;
        return;
    }

    make_sandbox();

    AppState *state = app_state_new();
    GtkWidget *panel = gui_metadata_panel_new(state);

    char path[512];
    snprintf(path, sizeof(path), "%s/link.txt", SANDBOX);

    /* resolve_target = 1 so the target's existence is reported. */
    FileMetadata meta;
    CHECK(fs_get_metadata(path, 1, &meta) == FS_OK);

    state->selected = meta;
    g_signal_emit_by_name(state, APP_STATE_SIGNAL_SELECTION_CHANGED);

    row_contains(panel, 2, "Symbolic link");
    row_contains(panel, 10, "report.txt");     /* the target      */
    row_contains(panel, 10, "target exists"); /* and it is alive  */

    g_object_unref(state);
}

/* ------------------------------------------------------------------ *
 * test_broken_symlink
 * ------------------------------------------------------------------ */
static void
test_broken_symlink(void)
{
    make_sandbox();

    char path[512];
    snprintf(path, sizeof(path), "%s/broken.lnk", SANDBOX);
    if (t_symlink("/nonexistent/target", path) != 0) {
        fprintf(stderr, "  (could not create the broken link, skipping)\n");
        g_skip++;
        return;
    }

    AppState *state = app_state_new();
    GtkWidget *panel = gui_metadata_panel_new(state);

    FileMetadata meta;
    CHECK(fs_get_metadata(path, 1, &meta) == FS_OK);

    state->selected = meta;
    g_signal_emit_by_name(state, APP_STATE_SIGNAL_SELECTION_CHANGED);

    row_contains(panel, 2, "Symbolic link");
    row_contains(panel, 10, "BROKEN LINK");

    g_object_unref(state);
}

/* ------------------------------------------------------------------ *
 * test_error_metadata
 * ------------------------------------------------------------------ */
static void
test_error_metadata(void)
{
    AppState *state = app_state_new();
    GtkWidget *panel = gui_metadata_panel_new(state);

    char path[512];
    snprintf(path, sizeof(path), "%s/does_not_exist", SANDBOX);

    FileMetadata meta;
    memset(&meta, 0, sizeof(meta));
    CHECK(fs_get_metadata(path, 0, &meta) != FS_OK);

    state->selected = meta;
    g_signal_emit_by_name(state, APP_STATE_SIGNAL_SELECTION_CHANGED);

    /* The panel shows the error message in the "Type" row. */
    row_contains(panel, 2, "not found");
    row_equals(panel, 3, "-");
    g_object_unref(state);
}

/* ------------------------------------------------------------------ *
 * test_out_of_range_row
 * ------------------------------------------------------------------ */
static void
test_out_of_range_row(void)
{
    AppState *state = app_state_new();
    GtkWidget *panel = gui_metadata_panel_new(state);

    CHECK(gui_metadata_panel_get_row_text(panel, -1) == NULL);
    CHECK(gui_metadata_panel_get_row_text(panel, 999) == NULL);
    CHECK(gui_metadata_panel_get_row_text(NULL, 0) == NULL);

    g_object_unref(state);
}

/* ------------------------------------------------------------------ *
 * main
 * ------------------------------------------------------------------ */
int
main(int argc, char **argv)
{
    /* A GUI test needs a display. Without one there is nothing to test. */
    if (!gtk_init_check(&argc, &argv)) {
        printf("==== gui metadata test ====\n");
        printf("  (no display available - test skipped)\n");
        printf("==== Result: 0 passed, 0 failed, 1 skipped ====\n");
        return 0;
    }

    printf("==== gui metadata test ====\n");
    t_sandbox_init("gui");

    make_sandbox();

    test_empty_panel();
    test_regular_file();
    test_directory();
    test_symlink();
    test_broken_symlink();
    test_error_metadata();
    test_out_of_range_row();

    rm_sandbox();

    printf("==== Result: %d passed, %d failed ====\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
