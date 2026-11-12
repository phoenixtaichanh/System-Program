/* ============================================================================
 * gui/main_window.c -- The application's main window.
 *
 * Layout
 *   +---------------------------------------------------------------+
 *   |  Toolbar : Back | Forward | Up | Home | Refresh | ...        |
 *   +---------------------------------------------------------------+
 *   |  Path bar (entry + Go)                                        |
 *   +---------------------+-----------------------------------------+
 *   |  Directory tree     |  File list                              |
 *   |  (left pane)        |  (centre pane)                          |
 *   +---------------------+-----------------------------------------+
 *   |  Details panel (metadata of the selected entry)               |
 *   +---------------------------------------------------------------+
 *   |  Activity log (collapsible)                                  |
 *   +---------------------------------------------------------------+
 * ============================================================================
 */

#include "gui_main_window.h"

#include "app_state.h"
#include "format_utils.h"
#include "fs_analysis.h"
#include "fs_filesystem_info.h"
#include "fs_listing.h"
#include "fs_metadata.h"
#include "fs_operations.h"
#include "fs_platform.h"
#include "fs_search.h"
#include "gui_dialogs.h"
#include "gui_directory_tree.h"
#include "gui_file_view.h"
#include "gui_metadata_panel.h"
#include "logger.h"
#include "path_utils.h"

#include <string.h>

/* The signal used to notify that the current path changed. */
#define SIGNAL_PATH_CHANGED APP_STATE_SIGNAL_PATH_CHANGED

typedef struct {
    AppState  *state;
    GtkWidget *window;
    GtkWidget *path_entry;
    GtkWidget *back_button;
    GtkWidget *forward_button;
    GtkWidget *up_button;
    GtkWidget *status_bar;
    GtkWidget *log_view;
    GtkWidget *log_expander;
    guint      status_context;
} MainWindow;

/* ------------------------------------------------------------------ *
 * Update the status bar and the navigation buttons.
 * ------------------------------------------------------------------ */
static void
refresh_navigation(MainWindow *mw)
{
    gtk_widget_set_sensitive(mw->back_button,
                             app_state_can_go_back(mw->state) ? TRUE : FALSE);
    gtk_widget_set_sensitive(mw->forward_button,
                             app_state_can_go_forward(mw->state) ? TRUE : FALSE);
}

/* ------------------------------------------------------------------ *
 * Append a line to the activity log view.
 * ------------------------------------------------------------------ */
static void
refresh_log(MainWindow *mw)
{
    static char buf[LOG_MSG_MAX * 8];
    size_t n = logger_recent(buf, sizeof(buf), 100);
    if (n == 0) {
        gtk_text_buffer_set_text(GTK_TEXT_BUFFER(
            gtk_text_view_get_buffer(GTK_TEXT_VIEW(mw->log_view))), "", -1);
        return;
    }
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(mw->log_view))), buf, -1);
}

/* ------------------------------------------------------------------ *
 * Load `path` into the application state and refresh the whole UI.
 * ------------------------------------------------------------------ */
static void
navigate_to(MainWindow *mw, const char *path)
{
    if (path == NULL || path[0] == '\0') return;

    app_state_set_path(mw->state, path);
    gtk_entry_set_text(GTK_ENTRY(mw->path_entry), path);
    g_signal_emit_by_name(mw->state, SIGNAL_PATH_CHANGED, path);

    logger_append("navigate to %s", path);
    refresh_log(mw);
    refresh_navigation(mw);
}

/* ------------------------------------------------------------------ *
 * Toolbar callbacks
 * ------------------------------------------------------------------ */
static void
on_back_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    MainWindow *mw = (MainWindow *)user_data;

    if (app_state_go_back(mw->state) == FS_OK) {
        gtk_entry_set_text(GTK_ENTRY(mw->path_entry), mw->state->current_path);
        g_signal_emit_by_name(mw->state, SIGNAL_PATH_CHANGED,
                              mw->state->current_path);
        refresh_log(mw);
        refresh_navigation(mw);
    }
}

static void
on_forward_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    MainWindow *mw = (MainWindow *)user_data;

    if (app_state_go_forward(mw->state) == FS_OK) {
        gtk_entry_set_text(GTK_ENTRY(mw->path_entry), mw->state->current_path);
        g_signal_emit_by_name(mw->state, SIGNAL_PATH_CHANGED,
                              mw->state->current_path);
        refresh_log(mw);
        refresh_navigation(mw);
    }
}

static void
on_up_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    MainWindow *mw = (MainWindow *)user_data;

    if (app_state_go_up(mw->state) == FS_OK) {
        gtk_entry_set_text(GTK_ENTRY(mw->path_entry), mw->state->current_path);
        g_signal_emit_by_name(mw->state, SIGNAL_PATH_CHANGED,
                              mw->state->current_path);
        refresh_log(mw);
        refresh_navigation(mw);
    }
}

static void
on_home_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    MainWindow *mw = (MainWindow *)user_data;
    navigate_to(mw, path_home_dir());
}

static void
on_refresh_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    MainWindow *mw = (MainWindow *)user_data;

    g_signal_emit_by_name(mw->state, SIGNAL_PATH_CHANGED,
                          mw->state->current_path);
    logger_append("refresh %s", mw->state->current_path);
    refresh_log(mw);
}

/* ------------------------------------------------------------------ *
 * Path entry: "Go" button and Enter key.
 * ------------------------------------------------------------------ */
static void
on_go_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    MainWindow *mw = (MainWindow *)user_data;

    const char *text = gtk_entry_get_text(GTK_ENTRY(mw->path_entry));
    if (text == NULL || text[0] == '\0') return;

    char resolved[FS_PATH_MAX];
    if (fs_plat_realpath(text, resolved, sizeof(resolved))) {
        navigate_to(mw, resolved);
    } else {
        /* The path may not exist yet: report the error. */
        gui_show_error(GTK_WINDOW(mw->window), "Cannot open folder",
                       text, FS_ERR_NOT_FOUND, 0);
    }
}

static void
on_path_entry_activate(GtkEntry *entry, gpointer user_data)
{
    (void)entry;
    MainWindow *mw = (MainWindow *)user_data;
    on_go_clicked(NULL, mw);
}

/* ------------------------------------------------------------------ *
 * "Analyze" button: run the recursive analyzer on the current folder.
 * ------------------------------------------------------------------ */
static void
on_analyze_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    MainWindow *mw = (MainWindow *)user_data;

    const char *path = mw->state->current_path;
    if (path == NULL || path[0] == '\0') return;

    DirStats stats;
    FSStatus s = fs_analyze_directory(path, &stats);
    if (s != FS_OK) {
        gui_show_error(GTK_WINDOW(mw->window), "Analysis failed", path,
                       s, errno);
        return;
    }

    char total[32], avg[32], largest[32];
    fmt_size_human(stats.total_bytes, total, sizeof(total));
    fmt_size_human(stats.avg_file_size, avg, sizeof(avg));
    fmt_size_human(stats.largest_file_size, largest, sizeof(largest));

    char body[4096];
    int pos = snprintf(body, sizeof(body),
                       "Folder: %s\n\n"
                       "Files       : %llu\n"
                       "Directories : %llu\n"
                       "Symlinks    : %llu\n"
                       "Other       : %llu\n\n"
                       "Total size  : %s (%llu bytes)\n"
                       "Average file: %s\n"
                       "Largest file: %s\n",
                       stats.path,
                       stats.total_files, stats.total_dirs,
                       stats.total_symlinks, stats.total_other,
                       total, stats.total_bytes, avg, largest);

    if (stats.largest_file_size > 0 && pos > 0 &&
        (size_t)pos < sizeof(body)) {
        pos += snprintf(body + pos, sizeof(body) - (size_t)pos,
                        "\nLargest: %s\n", stats.largest_file_path);
    }

    if (stats.top_files_count > 0 && pos > 0 &&
        (size_t)pos < sizeof(body)) {
        pos += snprintf(body + pos, sizeof(body) - (size_t)pos,
                        "\nTop %d largest files:\n", stats.top_files_count);
        for (int i = 0; i < stats.top_files_count && pos > 0 &&
                        (size_t)pos < sizeof(body); i++) {
            char fsize[32];
            fmt_size_human(stats.top_files[i].size, fsize, sizeof(fsize));
            pos += snprintf(body + pos, sizeof(body) - (size_t)pos,
                            "  %-10s %s\n", fsize, stats.top_files[i].path);
        }
    }

    logger_append("analyzed %s", path);
    refresh_log(mw);
    gui_show_info(GTK_WINDOW(mw->window), "Folder analysis", body);
}

/* ------------------------------------------------------------------ *
 * "Disk usage" button: show the file system capacity.
 * ------------------------------------------------------------------ */
static void
on_disk_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    MainWindow *mw = (MainWindow *)user_data;

    const char *path = mw->state->current_path;
    FSInfo info;
    FSStatus s = fs_get_filesystem_info(path, &info);
    if (s != FS_OK) {
        gui_show_error(GTK_WINDOW(mw->window), "Cannot read file system",
                       path, s, errno);
        return;
    }

    char total[32], fre[32], avail[32];
    fmt_size_human(info.total_bytes, total, sizeof(total));
    fmt_size_human(info.free_bytes, fre, sizeof(fre));
    fmt_size_human(info.available_bytes, avail, sizeof(avail));

    char body[4096];
    int pos = snprintf(body, sizeof(body),
                       "File system: %s\n\n"
                       "Total     : %s\n"
                       "Free      : %s\n"
                       "Available : %s\n"
                       "Used      : %.1f%%\n",
                       info.mount_path, total, fre, avail, info.usage_percent);

    if (info.total_inodes > 0 && pos > 0 && (size_t)pos < sizeof(body)) {
        snprintf(body + pos, sizeof(body) - (size_t)pos,
                 "\nInodes: %llu total, %llu free\n",
                 info.total_inodes, info.free_inodes);
    }

    gui_show_info(GTK_WINDOW(mw->window), "Disk usage", body);
}

/* ------------------------------------------------------------------ *
 * "Find" button: search the current tree for a name.
 * ------------------------------------------------------------------ */
static void
on_find_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    MainWindow *mw = (MainWindow *)user_data;

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Find files",
                                                    GTK_WINDOW(mw->window),
                                                    GTK_DIALOG_MODAL,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Find", GTK_RESPONSE_ACCEPT,
                                                    NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);

    GtkWidget *label = gtk_label_new("Name pattern ( '*' and '?' wildcards):");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);

    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(content), box);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const char *pattern = gtk_entry_get_text(GTK_ENTRY(entry));

        SearchResult results[512];
        int count = (int)(sizeof(results) / sizeof(results[0]));

        FSStatus s = fs_search(mw->state->current_path, pattern, 0, -1,
                               count, results, &count);
        if (s != FS_OK) {
            gui_show_error(GTK_WINDOW(mw->window), "Search failed",
                           mw->state->current_path, s, errno);
        } else {
            char body[8192];
            int pos = snprintf(body, sizeof(body),
                               "%d match%s for \"%s\"\n\n",
                               count, (count == 1) ? "" : "es", pattern);
            for (int i = 0; i < count && pos > 0 &&
                            (size_t)pos < sizeof(body); i++) {
                pos += snprintf(body + pos, sizeof(body) - (size_t)pos,
                                "%s\n", results[i].meta.path);
            }
            logger_append("find \"%s\" -> %d matches", pattern, count);
            refresh_log(mw);
            gui_show_info(GTK_WINDOW(mw->window), "Search results", body);
        }
    }

    gtk_widget_destroy(dialog);
}

/* ------------------------------------------------------------------ *
 * "New folder" button.
 * ------------------------------------------------------------------ */
static void
on_new_folder_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    MainWindow *mw = (MainWindow *)user_data;

    GtkWidget *dialog = gtk_dialog_new_with_buttons("New folder",
                                                    GTK_WINDOW(mw->window),
                                                    GTK_DIALOG_MODAL,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Create", GTK_RESPONSE_ACCEPT,
                                                    NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);

    GtkWidget *label = gtk_label_new("Folder name:");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);

    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(content), box);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const char *name = gtk_entry_get_text(GTK_ENTRY(entry));
        if (name != NULL && name[0] != '\0') {
            char path[FS_PATH_MAX];
            if (path_join(mw->state->current_path, name, path,
                          sizeof(path))) {
                FSStatus s = fs_mkdir(path, 0);
                if (s != FS_OK) {
                    gui_show_error(GTK_WINDOW(mw->window), "Cannot create folder",
                                   path, s, errno);
                } else {
                    logger_append("mkdir %s", path);
                    refresh_log(mw);
                    on_refresh_clicked(NULL, mw);
                }
            }
        }
    }

    gtk_widget_destroy(dialog);
}

/* ------------------------------------------------------------------ *
 * "Delete" button: remove the selected entry (after confirmation).
 * ------------------------------------------------------------------ */
static void
on_delete_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    MainWindow *mw = (MainWindow *)user_data;

    if (mw->state->selected.name[0] == '\0') {
        gui_show_info(GTK_WINDOW(mw->window), "Delete",
                      "Select a file or folder first.");
        return;
    }

    char body[FS_PATH_MAX + 128];
    snprintf(body, sizeof(body),
             "Permanently delete:\n\n%s\n\n"
             "Directories must be empty. This cannot be undone.",
             mw->state->selected.path);

    if (!gui_confirm(GTK_WINDOW(mw->window), "Confirm deletion", body)) {
        return;
    }

    FSStatus s = fs_remove(mw->state->selected.path);
    if (s != FS_OK) {
        gui_show_error(GTK_WINDOW(mw->window), "Cannot delete",
                       mw->state->selected.path, s, errno);
        return;
    }

    logger_append("rm %s", mw->state->selected.path);
    memset(&mw->state->selected, 0, sizeof(mw->state->selected));
    g_signal_emit_by_name(mw->state, APP_STATE_SIGNAL_SELECTION_CHANGED);
    refresh_log(mw);
    on_refresh_clicked(NULL, mw);
}

/* ------------------------------------------------------------------ *
 * "About" dialog.
 * ------------------------------------------------------------------ */
static void
on_about_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    MainWindow *mw = (MainWindow *)user_data;

    gui_show_info(GTK_WINDOW(mw->window), "About",
        "File System Explorer & Metadata Analyzer\n\n"
        "Version 1.0.0\n\n"
        "A GTK 3 file explorer with an integrated file system\n"
        "metadata analyzer.\n\n"
        "Built with C11, GTK 3 and the POSIX file system APIs.");
}

/* ------------------------------------------------------------------ *
 * Build the toolbar.
 * ------------------------------------------------------------------ */
static GtkWidget *
build_toolbar(MainWindow *mw)
{
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(bar), 4);

    struct {
        const char *label;
        const char *tip;
        GCallback   cb;
    } buttons[] = {
        { "<",     "Go back",        G_CALLBACK(on_back_clicked) },
        { ">",     "Go forward",     G_CALLBACK(on_forward_clicked) },
        { "^",     "Parent folder",  G_CALLBACK(on_up_clicked) },
        { "Home",  "Home folder",    G_CALLBACK(on_home_clicked) },
        { "Refresh","Refresh view",  G_CALLBACK(on_refresh_clicked) },
        { "Analyze","Analyze folder",G_CALLBACK(on_analyze_clicked) },
        { "Disk",  "Disk usage",     G_CALLBACK(on_disk_clicked) },
        { "Find",  "Find files",     G_CALLBACK(on_find_clicked) },
        { "New folder", "Create a folder", G_CALLBACK(on_new_folder_clicked) },
        { "Delete","Delete selection",G_CALLBACK(on_delete_clicked) },
        { "About", "About",          G_CALLBACK(on_about_clicked) },
    };

    for (unsigned i = 0; i < G_N_ELEMENTS(buttons); i++) {
        GtkWidget *b = gtk_button_new_with_label(buttons[i].label);
        gtk_widget_set_tooltip_text(b, buttons[i].tip);
        g_signal_connect(b, "clicked", buttons[i].cb, mw);
        gtk_box_pack_start(GTK_BOX(bar), b, FALSE, FALSE, 0);

        if (i == 0) {
            mw->back_button = b;
        } else if (i == 1) {
            mw->forward_button = b;
        } else if (i == 2) {
            mw->up_button = b;
        }
    }

    return bar;
}

/* ------------------------------------------------------------------ *
 * gui_main_window_new
 * ------------------------------------------------------------------ */
GtkWidget *
gui_main_window_new(const char *start_path)
{
    MainWindow *mw = (MainWindow *)g_new0(MainWindow, 1);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window),
                         "File System Explorer & Metadata Analyzer");
    gtk_window_set_default_size(GTK_WINDOW(window), 1150, 750);
    mw->window = window;

    /* ---- application state ---------------------------------------- */
    mw->state = app_state_new();

    /* The initial directory is the one requested on the command line; it
     * falls back to the user's home when it is missing or unusable. */
    {
        const char *initial = NULL;

        if (start_path != NULL && start_path[0] != '\0') {
            FsRawStat st;
            if (fs_plat_lstat(start_path, &st) == 0 && FS_S_ISDIR(st.mode)) {
                initial = start_path;
            }
        }
        if (initial == NULL) {
            initial = path_home_dir();
        }

        snprintf(mw->state->current_path, FS_PATH_MAX, "%s", initial);
        app_state_set_path(mw->state, mw->state->current_path);
    }

    /* ---- root layout ------------------------------------------------ */
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), root);

    /* ---- toolbar ----------------------------------------------------- */
    gtk_box_pack_start(GTK_BOX(root), build_toolbar(mw), FALSE, FALSE, 0);

    /* ---- path bar ---------------------------------------------------- */
    GtkWidget *path_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(path_bar), 4);

    GtkWidget *path_label = gtk_label_new("Path:");
    gtk_widget_set_halign(path_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(path_bar), path_label, FALSE, FALSE, 0);

    mw->path_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(mw->path_entry), mw->state->current_path);
    gtk_entry_set_activates_default(GTK_ENTRY(mw->path_entry), TRUE);
    gtk_box_pack_start(GTK_BOX(path_bar), mw->path_entry, TRUE, TRUE, 0);
    g_signal_connect(mw->path_entry, "activate",
                     G_CALLBACK(on_path_entry_activate), mw);

    GtkWidget *go_button = gtk_button_new_with_label("Go");
    g_signal_connect(go_button, "clicked", G_CALLBACK(on_go_clicked), mw);
    gtk_box_pack_start(GTK_BOX(path_bar), go_button, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(root), path_bar, FALSE, FALSE, 0);

    /* ---- centre: tree + file list ------------------------------------ */
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_position(GTK_PANED(paned), 260);

    GtkWidget *tree = gui_directory_tree_new(mw->state);
    gtk_paned_pack1(GTK_PANED(paned), tree, FALSE, TRUE);

    GtkWidget *files = gui_file_view_new(mw->state);
    gtk_paned_pack2(GTK_PANED(paned), files, TRUE, TRUE);

    gtk_box_pack_start(GTK_BOX(root), paned, TRUE, TRUE, 0);

    /* ---- details panel ------------------------------------------------ */
    GtkWidget *details = gui_metadata_panel_new(mw->state);
    gtk_box_pack_start(GTK_BOX(root), details, FALSE, FALSE, 0);

    /* ---- activity log -------------------------------------------------- */
    mw->log_expander = gtk_expander_new("Activity log");
    gtk_box_pack_start(GTK_BOX(root), mw->log_expander, FALSE, FALSE, 0);

    GtkWidget *log_scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(log_scrolled),
                                        GTK_SHADOW_IN);
    gtk_widget_set_size_request(log_scrolled, -1, 120);

    mw->log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(mw->log_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(mw->log_view), TRUE);
    gtk_container_add(GTK_CONTAINER(log_scrolled), mw->log_view);
    gtk_container_add(GTK_CONTAINER(mw->log_expander), log_scrolled);

    /* ---- status bar ---------------------------------------------------- */
    mw->status_bar = gtk_statusbar_new();
    mw->status_context = gtk_statusbar_get_context_id(
        GTK_STATUSBAR(mw->status_bar), "fs-explorer");
    gtk_statusbar_push(GTK_STATUSBAR(mw->status_bar), mw->status_context,
                       "Ready");
    gtk_box_pack_start(GTK_BOX(root), mw->status_bar, FALSE, FALSE, 0);

    /* ---- initial load ---------------------------------------------------- */
    g_signal_emit_by_name(mw->state, SIGNAL_PATH_CHANGED,
                          mw->state->current_path);
    refresh_navigation(mw);
    refresh_log(mw);

    logger_append("application started in %s", mw->state->current_path);
    refresh_log(mw);

    return window;
}
