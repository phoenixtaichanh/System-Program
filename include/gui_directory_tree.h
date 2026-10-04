/* ============================================================================
 * gui_directory_tree.h -- Left directory tree pane.
 * Implementation lands in PHASE 10.
 * ============================================================================
 */

#ifndef GUI_DIRECTORY_TREE_H
#define GUI_DIRECTORY_TREE_H

#include <gtk/gtk.h>
#include "app_state.h"

GtkWidget *gui_directory_tree_new(AppState *state);

#endif /* GUI_DIRECTORY_TREE_H */