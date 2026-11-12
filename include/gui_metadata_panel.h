/* ============================================================================
 * gui_metadata_panel.h -- Bottom metadata card.
 * Implementation lands in PHASE 11.
 * ============================================================================
 */

#ifndef GUI_METADATA_PANEL_H
#define GUI_METADATA_PANEL_H

#include <gtk/gtk.h>
#include "app_state.h"

GtkWidget *gui_metadata_panel_new(AppState *state);

/*
 * Test helper: return the text of the row at `row` (0 = Name ... 11 = Your
 * access), or NULL when the row does not exist. The returned string is owned
 * by the caller and must be freed with g_free().
 */
char *gui_metadata_panel_get_row_text(GtkWidget *panel, int row);

#endif /* GUI_METADATA_PANEL_H */