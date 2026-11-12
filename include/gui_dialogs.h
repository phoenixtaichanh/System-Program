/* ============================================================================
 * gui_dialogs.h -- Standardized error & confirm dialogs.
 * Implementation lands in PHASE 13.
 * ============================================================================
 */

#ifndef GUI_DIALOGS_H
#define GUI_DIALOGS_H

#include <gtk/gtk.h>
#include "fs_types.h"

void gui_show_error(GtkWindow *parent, const char *title,
                    const char *path, FSStatus status, int errno_code);

gboolean gui_confirm(GtkWindow *parent, const char *title, const char *body);

/* Show a read-only informational dialog (used for "about" and for the
 * results of long-running analyses). */
void gui_show_info(GtkWindow *parent, const char *title, const char *body);

#endif /* GUI_DIALOGS_H */