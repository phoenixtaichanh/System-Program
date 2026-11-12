/* ============================================================================
 * gui_main_window.h -- Builder for the application's main window.
 * ============================================================================
 */

#ifndef GUI_MAIN_WINDOW_H
#define GUI_MAIN_WINDOW_H

#include <gtk/gtk.h>

/*
 * Construct the application's main window.
 * Returns a GtkWidget* (a GtkWindow toplevel). The caller is responsible
 * for adding it to a GtkApplication via gtk_window_set_application()
 * and showing it.
 *
 * `start_path` is the directory the window opens in. When it is NULL, empty
 * or not a directory, the user's home directory is used instead.
 */
GtkWidget *gui_main_window_new(const char *start_path);

#endif /* GUI_MAIN_WINDOW_H */