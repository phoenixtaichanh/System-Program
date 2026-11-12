/* ============================================================================
 * app_state.c -- Shared application state.
 *
 * Holds the current working path, the back/forward navigation history and
 * the currently selected file. The GUI widgets read from this structure
 * and call the mutators below; the CLI keeps its own copy of the path.
 *
 * History model:
 *   - `history[]` stores every visited path in visit order.
 *   - `history_cursor` points at the entry that is currently displayed.
 *   - Going back decrements the cursor, going forward increments it.
 *   - Navigating to a *new* location truncates the forward history and
 *     appends the new path, exactly like a browser.
 * ============================================================================
 */

#include "app_state.h"
#include "path_utils.h"

#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ *
 * GObject boilerplate
 *
 * The boilerplate is written out by hand instead of using G_DEFINE_TYPE
 * because that macro generates a static function called "app_state_init",
 * which clashes with the public app_state_init() below.
 * ------------------------------------------------------------------ */
static void app_state_do_init      (GTypeInstance *instance, gpointer klass);
static void app_state_class_init   (gpointer klass, gpointer data);

GType
app_state_get_type(void)
{
    static GType type = 0;

    if (type == 0) {
        /* GTypeInfo layout (glib-2.0/gobject/gtype.h):
         *   class_size, base_init, base_finalize, class_init,
         *   class_finalize, class_data, instance_size, n_preallocs,
         *   instance_init, value_table
         * class_size and instance_size are guint16, so the instance
         * struct has to stay below 64 KiB. */
        static const GTypeInfo info = {
            (guint16)sizeof(AppStateClass), /* class_size     */
            NULL,                            /* base_init      */
            NULL,                            /* base_finalize  */
            app_state_class_init,            /* class_init     */
            NULL,                            /* class_finalize */
            NULL,                            /* class_data     */
            (guint16)sizeof(AppState),       /* instance_size  */
            0,                               /* n_preallocs    */
            app_state_do_init,               /* instance_init  */
            NULL                             /* value_table    */
        };

        type = g_type_register_static(G_TYPE_OBJECT, "AppState", &info, 0);
    }

    return type;
}

static void
app_state_finalize(GObject *object)
{
    AppState *state = (AppState *)object;

    /* Release the history entries. */
    for (int i = 0; i < state->history_count && i < HISTORY_MAX; i++) {
        g_free(state->history[i]);
        state->history[i] = NULL;
    }

    G_OBJECT_CLASS(g_type_class_peek(G_TYPE_OBJECT))->finalize(object);
}

static void
app_state_class_init(gpointer klass, gpointer data)
{
    (void)data;

    AppStateClass *state_class = (AppStateClass *)klass;
    G_OBJECT_CLASS(state_class)->finalize = app_state_finalize;

    /* Emitted whenever the current directory changes. */
    g_signal_new(APP_STATE_SIGNAL_PATH_CHANGED,
                 G_TYPE_FROM_CLASS(state_class),
                 G_SIGNAL_RUN_LAST,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__STRING,
                 G_TYPE_NONE, 1, G_TYPE_STRING);

    /* Emitted whenever the selected entry changes. */
    g_signal_new(APP_STATE_SIGNAL_SELECTION_CHANGED,
                 G_TYPE_FROM_CLASS(state_class),
                 G_SIGNAL_RUN_LAST,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__VOID,
                 G_TYPE_NONE, 0);
}

/* The GObject instance-initialisation hook. */
static void
app_state_do_init(GTypeInstance *instance, gpointer klass)
{
    (void)klass;
    app_state_init((AppState *)instance);
}

/* ------------------------------------------------------------------ *
 * app_state_new
 * ------------------------------------------------------------------ */
AppState *
app_state_new(void)
{
    return (AppState *)g_object_new(APP_STATE_TYPE, NULL);
}

/* ------------------------------------------------------------------ *
 * app_state_init
 * ------------------------------------------------------------------ */
void
app_state_init(AppState *state)
{
    if (state == NULL) return;

    memset(state->current_path, 0, sizeof(state->current_path));
    memset(state->history, 0, sizeof(state->history));
    memset(&state->selected, 0, sizeof(state->selected));

    state->history_count  = 0;
    state->history_cursor = -1;
    state->busy           = 0;
}
/* ------------------------------------------------------------------ *
 * app_state_set_path
 *
 * Records `path` as the new current location. If it differs from the
 * current one, the forward history is discarded and the new path is
 * appended.
 * ------------------------------------------------------------------ */
void
app_state_set_path(AppState *state, const char *path)
{
    if (state == NULL || path == NULL) return;

    snprintf(state->current_path, sizeof(state->current_path), "%s", path);

    /* Already at this location? Nothing to record. */
    if (state->history_count > 0 &&
        state->history_cursor >= 0 &&
        state->history[state->history_cursor] != NULL &&
        strcmp(state->history[state->history_cursor], path) == 0) {
        return;
    }

    /* Discard the forward history. */
    if (state->history_cursor >= 0 &&
        state->history_cursor < state->history_count - 1) {
        for (int i = state->history_cursor + 1; i < state->history_count; i++) {
            g_free(state->history[i]);
            state->history[i] = NULL;
        }
        state->history_count = state->history_cursor + 1;
    }

    /* Append, unless the history is full (then drop the oldest). */
    if (state->history_count >= HISTORY_MAX) {
        g_free(state->history[0]);
        for (int i = 1; i < HISTORY_MAX; i++) {
            state->history[i - 1] = state->history[i];
        }
        state->history[HISTORY_MAX - 1] = NULL;
        state->history_count = HISTORY_MAX - 1;
    }

    state->history[state->history_count] = g_strdup(path);
    state->history_count++;
    state->history_cursor = state->history_count - 1;
}

/* ------------------------------------------------------------------ *
 * app_state_can_go_back / app_state_can_go_forward
 * ------------------------------------------------------------------ */
int
app_state_can_go_back(const AppState *state)
{
    if (state == NULL) return 0;
    return (state->history_cursor > 0);
}

int
app_state_can_go_forward(const AppState *state)
{
    if (state == NULL) return 0;
    return (state->history_cursor >= 0 &&
            state->history_cursor < state->history_count - 1);
}

/* ------------------------------------------------------------------ *
 * app_state_go_back
 * ------------------------------------------------------------------ */
FSStatus
app_state_go_back(AppState *state)
{
    if (state == NULL) return FS_ERR_INVALID_PATH;
    if (!app_state_can_go_back(state)) return FS_ERR_NOT_FOUND;

    state->history_cursor--;

    const char *path = state->history[state->history_cursor];
    if (path == NULL) return FS_ERR_NOT_FOUND;

    snprintf(state->current_path, sizeof(state->current_path), "%s", path);
    return FS_OK;
}

/* ------------------------------------------------------------------ *
 * app_state_go_forward
 * ------------------------------------------------------------------ */
FSStatus
app_state_go_forward(AppState *state)
{
    if (state == NULL) return FS_ERR_INVALID_PATH;
    if (!app_state_can_go_forward(state)) return FS_ERR_NOT_FOUND;

    state->history_cursor++;

    const char *path = state->history[state->history_cursor];
    if (path == NULL) return FS_ERR_NOT_FOUND;

    snprintf(state->current_path, sizeof(state->current_path), "%s", path);
    return FS_OK;
}

/* ------------------------------------------------------------------ *
 * app_state_go_up
 *
 * Moves to the parent directory of the current path. The root directory
 * is its own parent, so navigating up from "/" is a no-op.
 * ------------------------------------------------------------------ */
FSStatus
app_state_go_up(AppState *state)
{
    if (state == NULL) return FS_ERR_INVALID_PATH;
    if (state->current_path[0] == '\0') return FS_ERR_NOT_FOUND;

    char parent[FS_PATH_MAX];
    if (!path_dirname(state->current_path, parent, sizeof(parent))) {
        return FS_ERR_INVALID_PATH;
    }

    app_state_set_path(state, parent);
    return FS_OK;
}
