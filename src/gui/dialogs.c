/* ============================================================================
 * gui/dialogs.c -- Standardized error & confirm dialogs.
 *
 * Every error the user can trigger goes through gui_show_error() so that
 * all dialogs look and behave the same way.
 * ============================================================================
 */

#include "gui_dialogs.h"

#include "format_utils.h"

#include <string.h>

/* ------------------------------------------------------------------ *
 * gui_show_error
 *
 * Shows a modal error dialog describing a failed operation on `path`.
 * ------------------------------------------------------------------ */
void
gui_show_error(GtkWindow *parent,
               const char *title,
               const char *path,
               FSStatus status,
               int errno_code)
{
    const char *message = fmt_status_message(status, errno_code);

    char body[1024];
    if (path != NULL && path[0] != '\0') {
        snprintf(body, sizeof(body), "%s\n\nPath: %s", message, path);
    } else {
        snprintf(body, sizeof(body), "%s", message);
    }

    GtkWidget *dialog = gtk_message_dialog_new(parent,
                                               GTK_DIALOG_MODAL,
                                               GTK_MESSAGE_ERROR,
                                               GTK_BUTTONS_CLOSE,
                                               "%s",
                                               body);
    gtk_window_set_title(GTK_WINDOW(dialog),
                         (title && title[0]) ? title : "File System Error");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

/* ------------------------------------------------------------------ *
 * gui_confirm
 *
 * Asks the user to confirm a potentially destructive action.
 * Returns TRUE when the user confirmed, FALSE otherwise.
 * ------------------------------------------------------------------ */
gboolean
gui_confirm(GtkWindow *parent, const char *title, const char *body)
{
    GtkWidget *dialog = gtk_message_dialog_new(parent,
                                               GTK_DIALOG_MODAL,
                                               GTK_MESSAGE_QUESTION,
                                               GTK_BUTTONS_YES_NO,
                                               "%s",
                                               (body && body[0]) ? body
                                                                : "Are you sure?");
    gtk_window_set_title(GTK_WINDOW(dialog),
                         (title && title[0]) ? title : "Please confirm");

    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    return (response == GTK_RESPONSE_YES) ? TRUE : FALSE;
}

/* ------------------------------------------------------------------ *
 * gui_show_info
 *
 * A read-only informational dialog (used for the "about" box and the
 * results of long-running analyses).
 * ------------------------------------------------------------------ */
void
gui_show_info(GtkWindow *parent, const char *title, const char *body)
{
    GtkWidget *dialog = gtk_message_dialog_new(parent,
                                               GTK_DIALOG_MODAL,
                                               GTK_MESSAGE_INFO,
                                               GTK_BUTTONS_CLOSE,
                                               "%s",
                                               (body && body[0]) ? body : "");
    gtk_window_set_title(GTK_WINDOW(dialog),
                         (title && title[0]) ? title : "Information");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}
