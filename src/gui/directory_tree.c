/* ============================================================================
 * gui/directory_tree.c -- Left directory tree pane.
 *
 * A GtkTreeView with a single column showing the directory name. Each row
 * stores the full path of the directory it represents.
 *
 * The tree is lazily expanded: clicking the expander loads the children of
 * that directory on demand, so a huge tree never has to be walked up front.
 * ============================================================================
 */

#include "gui_directory_tree.h"

#include "app_state.h"

#include "fs_listing.h"
#include "path_utils.h"

#include <string.h>

/* Maximum number of children loaded for one directory. */
#define TREE_MAX_CHILDREN 512

/* Column indices. */
enum {
    COL_NAME = 0,
    COL_PATH,
    N_COLS
};

/* The signal used to notify that the current path changed. */
#define SIGNAL_PATH_CHANGED APP_STATE_SIGNAL_PATH_CHANGED

typedef struct {
    AppState  *state;
    GtkWidget *tree_view;
    GtkWidget *scrolled;
} DirTree;

/* ------------------------------------------------------------------ *
 * Build the children of `parent_row` (or the root level when
 * `parent_row` is NULL).
 * ------------------------------------------------------------------ */
static void
populate(GtkTreeStore *store, GtkTreeIter *parent_iter,
         const char *path, int depth)
{
    if (depth > 12) return;   /* do not descend forever */

    DirEntry *entries = (DirEntry *)calloc(TREE_MAX_CHILDREN,
                                           sizeof(DirEntry));
    if (entries == NULL) return;

    int count = TREE_MAX_CHILDREN;
    if (fs_list_dir(path, entries, &count) != FS_OK) {
        free(entries);
        return;
    }

    for (int i = 0; i < count; i++) {
        if (entries[i].meta.type != FS_TYPE_DIRECTORY) continue;

        GtkTreeIter iter;
        gtk_tree_store_append(store, &iter, parent_iter);
        gtk_tree_store_set(store, &iter,
                           COL_NAME, entries[i].meta.name,
                           COL_PATH, entries[i].meta.path,
                           -1);
    }

    free(entries);
}

/* ------------------------------------------------------------------ *
 * Row activation: the user double-clicked a directory -> navigate.
 * ------------------------------------------------------------------ */
static void
on_row_activated(GtkTreeView *view, GtkTreePath *treepath,
                 GtkTreeViewColumn *column, gpointer user_data)
{
    (void)treepath;
    (void)column;

    DirTree *tree = (DirTree *)user_data;
    GtkTreeModel *model = gtk_tree_view_get_model(view);
    GtkTreeIter iter;

    if (!gtk_tree_model_get_iter(model, &iter, treepath)) return;

    gchar *path = NULL;
    gtk_tree_model_get(model, &iter, COL_PATH, &path, -1);
    if (path == NULL) return;

    if (tree->state != NULL) {
        app_state_set_path(tree->state, path);
        g_signal_emit_by_name(tree->state, SIGNAL_PATH_CHANGED, path);
    }
    g_free(path);
}

/* ------------------------------------------------------------------ *
 * Row selection: show the selected directory in the file view.
 * ------------------------------------------------------------------ */
static void
on_selection_changed(GtkTreeSelection *selection, gpointer user_data)
{
    DirTree *tree = (DirTree *)user_data;
    GtkTreeModel *model = NULL;
    GtkTreeIter iter;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) return;

    gchar *path = NULL;
    gtk_tree_model_get(model, &iter, COL_PATH, &path, -1);
    if (path == NULL) return;

    if (tree->state != NULL) {
        app_state_set_path(tree->state, path);
        g_signal_emit_by_name(tree->state, SIGNAL_PATH_CHANGED, path);
    }
    g_free(path);
}

/* ------------------------------------------------------------------ *
 * Tree row expanded: load the children lazily.
 * ------------------------------------------------------------------ */
static void
on_row_expanded(GtkTreeView *view, GtkTreePath *treepath,
                GtkTreeIter *iter, gpointer user_data)
{
    (void)treepath;
    (void)user_data;

    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(view));
    GtkTreeIter parent;

    if (!gtk_tree_model_iter_parent(model, &parent, iter)) return;

    gchar *path = NULL;
    gtk_tree_model_get(model, &parent, COL_PATH, &path, -1);
    if (path == NULL) return;

    /* Only populate once: check whether the row already has children. */
    GtkTreeIter child;
    if (gtk_tree_model_iter_children(model, &child, &parent)) {
        g_free(path);
        return;
    }

    populate(GTK_TREE_STORE(model), &parent, path, 1);
    g_free(path);
}

/* ------------------------------------------------------------------ *
 * gui_directory_tree_new
 * ------------------------------------------------------------------ */
GtkWidget *
gui_directory_tree_new(AppState *state)
{
    DirTree *tree = (DirTree *)g_new0(DirTree, 1);
    tree->state = state;

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled),
                                        GTK_SHADOW_IN);

    GtkTreeStore *store = gtk_tree_store_new(N_COLS,
                                             G_TYPE_STRING,   /* COL_NAME */
                                             G_TYPE_STRING);  /* COL_PATH */

    GtkWidget *view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(view), TRUE);
    gtk_tree_view_set_search_column(GTK_TREE_VIEW(view), COL_NAME);
    gtk_tree_view_set_level_indentation(GTK_TREE_VIEW(view), 16);

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *column =
        gtk_tree_view_column_new_with_attributes("Folders",
                                                 renderer,
                                                 "text", COL_NAME,
                                                 NULL);
    gtk_tree_view_column_set_expand(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(view), column);

    gtk_container_add(GTK_CONTAINER(scrolled), view);

    /* Populate the root level with the top-level directories. */
    populate(store, NULL, "/", 0);

    g_signal_connect(view, "row-activated",
                     G_CALLBACK(on_row_activated), tree);
    g_signal_connect(view, "row-expanded",
                     G_CALLBACK(on_row_expanded), tree);

    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
    g_signal_connect(selection, "changed",
                     G_CALLBACK(on_selection_changed), tree);

    tree->tree_view = view;
    tree->scrolled  = scrolled;

    return scrolled;
}
