/* ============================================================================
 * gui/main_window.c -- Minimal main window for Phase 1.
 *
 * Only builds an empty GtkApplicationWindow so we can verify the toolchain
 * (GTK detection, pkg-config flags, -Wall -Wextra -Wpedantic) compiles cleanly.
 * ============================================================================
 */

#include "gui_main_window.h"

GtkWidget *
gui_main_window_new(void)
{
    GtkWidget *window;
    GtkWidget *label;

    /* Use a plain GtkWindow (toplevel) instead of GtkApplicationWindow here.
     * The application binding is done in main() after the widget exists,
     * which lets gtk_window_set_application() pass its assertion. */
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window),
                         "File System Explorer & Metadata Analyzer");
    gtk_window_set_default_size(GTK_WINDOW(window), 1100, 700);

    label = gtk_label_new(
        "File System Explorer\n\n"
        "Phase 1: toolchain verified. UI lands here in later phases.");
    gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(label, GTK_ALIGN_CENTER);

    gtk_container_add(GTK_CONTAINER(window), label);

    return window;
}