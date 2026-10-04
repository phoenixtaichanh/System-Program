/* ============================================================================
 * gui_main_window.h -- Builder for the application's main window.
 * ============================================================================
 */

#ifndef GUI_MAIN_WINDOW_H
#define GUI_MAIN_WINDOW_H

#include <gtk/gtk.h>

/*
 * Construct the application's main window.
 * The returned widget is a GtkApplicationWindow. The caller is responsible
 * for adding it to a GtkApplication and showing it.
 */
GtkWidget *gui_main_window_new(void);

#endif /* GUI_MAIN_WINDOW_H */