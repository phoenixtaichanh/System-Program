/* ============================================================================
 * gui_file_view.h -- Central file list widget.
 * Implementation lands in PHASE 10.
 * ============================================================================
 */

#ifndef GUI_FILE_VIEW_H
#define GUI_FILE_VIEW_H

#include <gtk/gtk.h>
#include "app_state.h"

/* Create the central file list view. Returns a GtkWidget* (a GtkPaned
 * container holding the tree view). The view internally subscribes to
 * path-change signals emitted on `state`. */
GtkWidget *gui_file_view_new(AppState *state);

#endif /* GUI_FILE_VIEW_H */