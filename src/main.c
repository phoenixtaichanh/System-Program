/* ============================================================================
 * main.c -- Application entry point
 *
 * Phase 1: create an empty GTK application window.
 * Later phases will fill this window with a toolbar, breadcrumb, paned
 * directory tree / file list, metadata panel, and activity log.
 * ============================================================================
 */

#include <gtk/gtk.h>

/* Forward declaration of the main window builder. The actual implementation
 * lives in src/gui/main_window.c and is introduced in Phase 9. */
extern GtkWidget *gui_main_window_new(void);

static void
on_activate(GtkApplication *app, gpointer user_data)
{
    (void)user_data;

    GtkWidget *window = gui_main_window_new();
    gtk_window_set_application(GTK_WINDOW(window), app);
    gtk_widget_show_all(window);
}

int
main(int argc, char *argv[])
{
    GtkApplication *app;
    int             status;

    app = gtk_application_new(
        "vn.vku.fs_explorer",      /* Application ID */
        G_APPLICATION_FLAGS_NONE);

    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);

    status = g_application_run(G_APPLICATION(app), argc, argv);

    g_object_unref(app);
    return status;
}