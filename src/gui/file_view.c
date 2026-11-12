/* ============================================================================
 * gui/file_view.c -- Central file list widget.
 *
 * A GtkTreeView with one row per directory entry and the following
 * columns:
 *
 *   Name | Type | Size | Modified | Permissions | Owner
 *
 * The list is sorted by name and refreshed whenever the application's
 * current path changes.
 * ============================================================================
 */

#include "gui_file_view.h"

#include "app_state.h"

#include "format_utils.h"
#include "fs_listing.h"
#include "fs_metadata.h"
#include "path_utils.h"

#include <string.h>

/* Maximum number of rows displayed at once. */
#define FILE_VIEW_MAX_ROWS 20000

/* Column indices. */
enum {
    COL_NAME = 0,
    COL_TYPE,
    COL_SIZE,
    COL_MODIFIED,
    COL_PERMS,
    COL_OWNER,
    COL_PATH,
    COL_IS_DIR,
    N_COLS
};

/* The signal used to notify that the current path changed. */
#define SIGNAL_PATH_CHANGED   APP_STATE_SIGNAL_PATH_CHANGED

/* The signal used to notify that the selection changed. */
#define SIGNAL_SELECTION_CHANGED APP_STATE_SIGNAL_SELECTION_CHANGED

typedef struct {
    AppState  *state;
    GtkWidget *tree_view;
    GtkWidget *scrolled;
    GtkWidget *status_label;
    GtkListStore *store;
    /* Guards against re-entrancy while we update the model ourselves. */
    int        updating;
} FileView;

/* ------------------------------------------------------------------ *
 * Sort comparator: directories first, then by name (case-sensitive).
 * ------------------------------------------------------------------ */
static int
cmp_entries(const void *pa, const void *pb)
{
    const DirEntry *a = (const DirEntry *)pa;
    const DirEntry *b = (const DirEntry *)pb;

    int a_dir = (a->meta.type == FS_TYPE_DIRECTORY) ? 1 : 0;
    int b_dir = (b->meta.type == FS_TYPE_DIRECTORY) ? 1 : 0;

    if (a_dir != b_dir) {
        return (a_dir > b_dir) ? -1 : 1;   /* directories first */
    }
    return strcmp(a->meta.name, b->meta.name);
}

/* ------------------------------------------------------------------ *
 * Fill the model with the contents of `path`.
 * ------------------------------------------------------------------ */
static void
load_directory(FileView *view, const char *path)
{
    GtkTreeIter iter;

    gtk_list_store_clear(view->store);

    DirEntry *entries = (DirEntry *)calloc(FILE_VIEW_MAX_ROWS,
                                           sizeof(DirEntry));
    if (entries == NULL) return;

    int count = FILE_VIEW_MAX_ROWS;
    FSStatus s = fs_list_dir(path, entries, &count);

    if (s != FS_OK) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Cannot open %s: %s",
                 path, fmt_status_message(s, errno));
        gtk_label_set_text(GTK_LABEL(view->status_label), msg);
        free(entries);
        return;
    }

    /* Directories first, then everything else, each group by name. */
    if (count > 1) {
        qsort(entries, (size_t)count, sizeof(DirEntry), cmp_entries);
    }

    for (int i = 0; i < count; i++) {
        const FileMetadata *m = &entries[i].meta;
        char size[32] = "-";
        char mtime[32] = "-";

        if (m->status == FS_OK) {
            fmt_size_human((unsigned long long)m->size, size, sizeof(size));
            fmt_time_local(m->mtime, mtime, sizeof(mtime));
        }

        /* A broken symlink is flagged so the user can spot it. */
        char type[64];
        snprintf(type, sizeof(type), "%s", fmt_type_label(m->type));
        if (m->type == FS_TYPE_SYMLINK && m->link_target_exists == -1) {
            snprintf(type, sizeof(type), "%s (broken)", fmt_type_label(m->type));
        }

        gtk_list_store_append(view->store, &iter);
        gtk_list_store_set(view->store, &iter,
                           COL_NAME,     m->name,
                           COL_TYPE,     type,
                           COL_SIZE,     size,
                           COL_MODIFIED, mtime,
                           COL_PERMS,    m->perm_symbolic,
                           COL_OWNER,    m->owner,
                           COL_PATH,     m->path,
                           COL_IS_DIR,   (m->type == FS_TYPE_DIRECTORY) ? 1 : 0,
                           -1);
    }

    char msg[512];
    snprintf(msg, sizeof(msg), "%d item%s%s", count,
             (count == 1) ? "" : "s",
             (count == FILE_VIEW_MAX_ROWS) ? " (truncated)" : "");
    gtk_label_set_text(GTK_LABEL(view->status_label), msg);

    free(entries);
}

/* ------------------------------------------------------------------ *
 * Path changed -> reload the listing.
 * ------------------------------------------------------------------ */
static void
on_path_changed(AppState *state, gchar *path, gpointer user_data)
{
    (void)state;
    FileView *view = (FileView *)user_data;

    if (path == NULL || path[0] == '\0') return;
    load_directory(view, path);
}

/* ------------------------------------------------------------------ *
 * Selection changed -> publish the selected entry to the app state.
 * ------------------------------------------------------------------ */
static void
on_view_selection_changed(GtkTreeSelection *selection, gpointer user_data)
{
    FileView *view = (FileView *)user_data;

    if (view->updating) return;

    GtkTreeModel *model = NULL;
    GtkTreeIter iter;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) return;

    gchar *path = NULL;
    gtk_tree_model_get(model, &iter, COL_PATH, &path, -1);
    if (path == NULL) return;

    /* Fetch the full metadata of the selected entry. */
    FileMetadata meta;
    if (fs_get_metadata(path, 1, &meta) == FS_OK) {
        view->state->selected = meta;
    } else {
        /* Keep the name/path even when the stat failed. */
        memset(&view->state->selected, 0, sizeof(view->state->selected));
        snprintf(view->state->selected.name,
                 sizeof(view->state->selected.name), "%s",
                 path_basename(path));
        snprintf(view->state->selected.path,
                 sizeof(view->state->selected.path), "%s", path);
        view->state->selected.status     = meta.status;
        view->state->selected.errno_code = meta.errno_code;
    }

    g_signal_emit_by_name(view->state, SIGNAL_SELECTION_CHANGED);

    g_free(path);
}

/* ------------------------------------------------------------------ *
 * Row activated: enter a directory, or do nothing for files.
 * ------------------------------------------------------------------ */
static void
on_row_activated(GtkTreeView *tree_view, GtkTreePath *treepath,
                 GtkTreeViewColumn *column, gpointer user_data)
{
    (void)tree_view;
    (void)treepath;
    (void)column;

    FileView *view = (FileView *)user_data;
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(view->tree_view));
    GtkTreeIter iter;

    if (!gtk_tree_model_get_iter(model, &iter, treepath)) return;

    gint is_dir = 0;
    gchar *path = NULL;
    gtk_tree_model_get(model, &iter, COL_PATH, &path, COL_IS_DIR, &is_dir, -1);
    if (path == NULL) return;

    if (is_dir) {
        app_state_set_path(view->state, path);
        g_signal_emit_by_name(view->state, SIGNAL_PATH_CHANGED, path);
    }

    g_free(path);
}

/* ------------------------------------------------------------------ *
 * gui_file_view_new
 * ------------------------------------------------------------------ */
GtkWidget *
gui_file_view_new(AppState *state)
{
    FileView *view = (FileView *)g_new0(FileView, 1);
    view->state = state;

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled),
                                        GTK_SHADOW_IN);

    GtkListStore *store = gtk_list_store_new(N_COLS,
                                             G_TYPE_STRING,   /* NAME     */
                                             G_TYPE_STRING,   /* TYPE     */
                                             G_TYPE_STRING,   /* SIZE     */
                                             G_TYPE_STRING,   /* MODIFIED */
                                             G_TYPE_STRING,   /* PERMS    */
                                             G_TYPE_STRING,   /* OWNER    */
                                             G_TYPE_STRING,   /* PATH     */
                                             G_TYPE_INT);     /* IS_DIR   */
    view->store = store;

    GtkWidget *tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(tree_view), TRUE);
    gtk_tree_view_set_search_column(GTK_TREE_VIEW(tree_view), COL_NAME);
    gtk_tree_view_set_headers_clickable(GTK_TREE_VIEW(tree_view), TRUE);

    struct {
        const char *title;
        int         col;
        gboolean    expand;
        gboolean    right;
    } columns[] = {
        { "Name",       COL_NAME,     TRUE,  FALSE },
        { "Type",       COL_TYPE,     FALSE, FALSE },
        { "Size",       COL_SIZE,     FALSE, TRUE  },
        { "Modified",   COL_MODIFIED, FALSE, FALSE },
        { "Permissions",COL_PERMS,    FALSE, FALSE },
        { "Owner",      COL_OWNER,    FALSE, FALSE },
    };

    for (unsigned i = 0; i < G_N_ELEMENTS(columns); i++) {
        GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
        if (columns[i].right) {
            g_object_set(renderer, "xalign", 1.0, NULL);
        }
        GtkTreeViewColumn *column =
            gtk_tree_view_column_new_with_attributes(columns[i].title,
                                                     renderer,
                                                     "text", columns[i].col,
                                                     NULL);
        gtk_tree_view_column_set_resizable(column, TRUE);
        gtk_tree_view_column_set_sort_column_id(column, columns[i].col);
        if (columns[i].expand) {
            gtk_tree_view_column_set_expand(column, TRUE);
        }
        gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);
    }

    /* Directories are sorted first; the ordering is applied in C before
     * the rows are inserted (see cmp_entries). */

    gtk_container_add(GTK_CONTAINER(scrolled), tree_view);

    GtkWidget *status = gtk_label_new("0 items");
    gtk_widget_set_halign(status, GTK_ALIGN_START);
    gtk_widget_set_margin_start(status, 6);
    gtk_widget_set_margin_bottom(status, 4);

    gtk_box_pack_start(GTK_BOX(box), scrolled, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), status, FALSE, FALSE, 0);

    g_signal_connect(tree_view, "row-activated",
                     G_CALLBACK(on_row_activated), view);

    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
    g_signal_connect(selection, "changed",
                     G_CALLBACK(on_view_selection_changed), view);

    /* Subscribe to path changes. */
    if (state != NULL) {
        g_signal_connect(state, SIGNAL_PATH_CHANGED,
                         G_CALLBACK(on_path_changed), view);
    }

    view->tree_view   = tree_view;
    view->scrolled    = scrolled;
    view->status_label = status;

    return box;
}
