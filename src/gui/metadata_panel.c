/* ============================================================================
 * gui/metadata_panel.c -- Bottom metadata card.
 *
 * Shows the details of the currently selected entry: name, type, size,
 * permissions, owner, timestamps, inode, link target and the permission
 * class of the current user.
 *
 * The panel subscribes to the application state's "selection changed"
 * signal so it refreshes automatically.
 * ============================================================================
 */

#include "gui_metadata_panel.h"

#include "app_state.h"

#include "format_utils.h"
#include "fs_metadata.h"
#include "fs_permissions.h"
#include "fs_symlink.h"

#include <string.h>

/* ------------------------------------------------------------------ *
 * Widget layout
 *
 *   +--------------------------------------------------+
 *   |  Name      : report.txt                          |
 *   |  Path      : /home/user/docs/report.txt          |
 *   |  Type      : Regular file                        |
 *   |  Size      : 1.4 KB (1432 bytes)                 |
 *   |  Mode      : -rw-r--r-- (0644)                   |
 *   |  Owner     : alice : staff                       |
 *   |  Modified  : 2026-10-04 15:42:18                |
 *   |  ...                                               |
 *   +--------------------------------------------------+
 * ------------------------------------------------------------------ */

#define META_ROWS 12

typedef struct {
    GtkWidget *grid;
    GtkWidget *labels[META_ROWS];
    AppState  *state;
} MetaPanel;

/* The signal used to notify that the selection changed. */
#define SIGNAL_SELECTION_CHANGED APP_STATE_SIGNAL_SELECTION_CHANGED

/* ------------------------------------------------------------------ *
 * Set the value of the row at `row` to `text`.
 * ------------------------------------------------------------------ */
static void
set_row(MetaPanel *p, int row, const char *text)
{
    if (row < 0 || row >= META_ROWS) return;
    gtk_label_set_text(GTK_LABEL(p->labels[row]),
                       (text && text[0]) ? text : "-");
}

/* ------------------------------------------------------------------ *
 * Refresh the panel from the selected entry.
 * ------------------------------------------------------------------ */
static void
refresh_panel(MetaPanel *p)
{
    if (p == NULL || p->state == NULL) return;

    const FileMetadata *m = &p->state->selected;

    if (m->name[0] == '\0') {
        set_row(p, 0,  "No selection");
        for (int i = 1; i < META_ROWS; i++) set_row(p, i, "-");
        return;
    }

    set_row(p, 0, m->name);
    set_row(p, 1, m->path);

    if (m->status != FS_OK) {
        set_row(p, 2, fmt_status_message(m->status, m->errno_code));
        for (int i = 3; i < META_ROWS; i++) set_row(p, i, "-");
        return;
    }

    set_row(p, 2, fmt_type_label(m->type));

    char buf[FS_PATH_MAX];

    /* Size. */
    if (m->type == FS_TYPE_DIRECTORY) {
        snprintf(buf, sizeof(buf), "directory");
    } else {
        char human[32];
        fmt_size_human((unsigned long long)m->size, human, sizeof(human));
        snprintf(buf, sizeof(buf), "%s (%lld bytes)", human, (long long)m->size);
    }
    set_row(p, 3, buf);

    /* Mode. */
    snprintf(buf, sizeof(buf), "%s  (%s)", m->perm_symbolic, m->perm_octal);
    set_row(p, 4, buf);

    /* Owner. */
    snprintf(buf, sizeof(buf), "%s : %s", m->owner, m->group);
    set_row(p, 5, buf);

    /* Timestamps. */
    char t[32];
    fmt_time_local(m->mtime, t, sizeof(t));
    snprintf(buf, sizeof(buf), "%s", t);
    set_row(p, 6, buf);

    fmt_time_local(m->atime, t, sizeof(t));
    snprintf(buf, sizeof(buf), "%s", t);
    set_row(p, 7, buf);

    fmt_time_local(m->ctime, t, sizeof(t));
    snprintf(buf, sizeof(buf), "%s", t);
    set_row(p, 8, buf);

    /* Inode / links / blocks. */
    snprintf(buf, sizeof(buf),
             "inode: %llu   links: %llu   blocks: %llu x %llu",
             (unsigned long long)m->inode,
             (unsigned long long)m->nlinks,
             (unsigned long long)m->blocks,
             (unsigned long long)m->block_size);
    set_row(p, 9, buf);

    /* Link target (symlinks only). */
    if (m->type == FS_TYPE_SYMLINK) {
        if (m->link_target[0] != '\0') {
            /* The target can be up to FS_PATH_MAX bytes long, so the
             * annotation is appended into a separate buffer. */
            char note[32];
            if (m->link_target_exists == 1) {
                snprintf(note, sizeof(note), "  (target exists)");
            } else if (m->link_target_exists == -1) {
                snprintf(note, sizeof(note), "  (BROKEN LINK)");
            } else {
                note[0] = '\0';
            }
            snprintf(buf, sizeof(buf), "%s%s", m->link_target, note);
        } else {
            snprintf(buf, sizeof(buf), "(unreadable)");
        }
    } else {
        snprintf(buf, sizeof(buf), "-");
    }
    set_row(p, 10, buf);

    /* Permission summary for the current user. */
    FsPermInfo perm;
    if (fs_get_permissions(m->path, &perm) == FS_OK) {
        const char *klass;
        switch (fs_perm_effective_class(&perm)) {
        case FS_PERM_CLASS_OWNER: klass = "owner"; break;
        case FS_PERM_CLASS_GROUP: klass = "group"; break;
        case FS_PERM_CLASS_OTHER:
        default:                  klass = "other"; break;
        }

        FsPermBits bits = fs_perm_effective_bits(&perm);
        char perms[4];
        snprintf(perms, sizeof(perms), "%s%s%s",
                 bits.read ? "r" : "-",
                 bits.write ? "w" : "-",
                 bits.execute ? "x" : "-");

        snprintf(buf, sizeof(buf), "%s  (you are: %s)", perms, klass);
    } else {
        snprintf(buf, sizeof(buf), "-");
    }
    set_row(p, 11, buf);
}

/* ------------------------------------------------------------------ *
 * Signal handler for the selection-changed signal.
 * ------------------------------------------------------------------ */
static void
on_selection_changed(AppState *state, gpointer user_data)
{
    (void)state;
    refresh_panel((MetaPanel *)user_data);
}

/* ------------------------------------------------------------------ *
 * gui_metadata_panel_new
 * ------------------------------------------------------------------ */
GtkWidget *
gui_metadata_panel_new(AppState *state)
{
    MetaPanel *p = (MetaPanel *)g_new0(MetaPanel, 1);
    p->state = state;

    GtkWidget *frame = gtk_frame_new("Details");
    p->grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(p->grid), 2);
    gtk_grid_set_column_spacing(GTK_GRID(p->grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(p->grid), 8);

    static const char *titles[META_ROWS] = {
        "Name", "Path", "Type", "Size", "Mode", "Owner",
        "Modified", "Accessed", "Changed", "Inode / Links / Blocks",
        "Link target", "Your access"
    };

    for (int i = 0; i < META_ROWS; i++) {
        GtkWidget *caption = gtk_label_new(titles[i]);
        GtkWidget *value  = gtk_label_new("-");

        gtk_widget_set_halign(caption, GTK_ALIGN_START);
        gtk_widget_set_halign(value, GTK_ALIGN_START);
        gtk_label_set_xalign(GTK_LABEL(caption), 0.0f);
        gtk_label_set_xalign(GTK_LABEL(value), 0.0f);

        /* Make the value selectable so the user can copy it. */
        gtk_label_set_selectable(GTK_LABEL(value), TRUE);
        gtk_label_set_selectable(GTK_LABEL(caption), TRUE);

        /* Give the value column all the remaining space. */
        gtk_label_set_ellipsize(GTK_LABEL(value), PANGO_ELLIPSIZE_MIDDLE);
        gtk_label_set_max_width_chars(GTK_LABEL(value), 70);

        gtk_grid_attach(GTK_GRID(p->grid), caption, 0, i, 1, 1);
        gtk_grid_attach(GTK_GRID(p->grid), value,  1, i, 1, 1);

        p->labels[i] = value;
    }

    gtk_container_add(GTK_CONTAINER(frame), p->grid);

    /* Subscribe to selection changes. */
    if (state != NULL) {
        g_signal_connect(state, SIGNAL_SELECTION_CHANGED,
                         G_CALLBACK(on_selection_changed), p);
    }

    refresh_panel(p);

    return frame;
}

/* ------------------------------------------------------------------ *
 * gui_metadata_panel_get_row_text
 *
 * Test helper: read back the text shown in a row of the panel.
 * ------------------------------------------------------------------ */
char *
gui_metadata_panel_get_row_text(GtkWidget *panel, int row)
{
    if (panel == NULL || row < 0 || row >= META_ROWS) return NULL;

    /* The panel widget is a GtkFrame; the grid is its child and the labels
     * live in the second column. */
    GtkWidget *grid = gtk_bin_get_child(GTK_BIN(panel));
    if (grid == NULL) return NULL;

    GtkWidget *label = gtk_grid_get_child_at(GTK_GRID(grid), 1, row);
    if (label == NULL) return NULL;

    const char *text = gtk_label_get_text(GTK_LABEL(label));
    return (text != NULL) ? g_strdup(text) : NULL;
}
