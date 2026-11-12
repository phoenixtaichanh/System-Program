/* ============================================================================
 * app_state.h -- Shared application state.
 *
 * AppState is a GObject so that the widgets can subscribe to its signals
 * ("fs-path-changed" and "fs-selection-changed"). Using a GObject also
 * gives the state a well defined lifetime that the widgets can hook into.
 * ============================================================================
 */

#ifndef APP_STATE_H
#define APP_STATE_H

#include "fs_types.h"

#include <glib-object.h>

G_BEGIN_DECLS

#define HISTORY_MAX 64

/* Signal emitted when the current directory changes.
 * The signal carries the new path as its first argument (gchar *). */
#define APP_STATE_SIGNAL_PATH_CHANGED "fs-path-changed"

/* Signal emitted when the selected entry changes.
 * The signal carries no arguments. */
#define APP_STATE_SIGNAL_SELECTION_CHANGED "fs-selection-changed"

#define APP_STATE_TYPE (app_state_get_type())

GType app_state_get_type(void);

typedef struct _AppState      AppState;
typedef struct _AppStateClass AppStateClass;

struct _AppState {
    GObject parent_instance;

    char  current_path[FS_PATH_MAX];

    /* The history holds dynamically allocated paths. Storing them as
     * fixed-size arrays would make the struct far too large for a
     * GObject (its size is limited to 16 bits). */
    gchar *history[HISTORY_MAX];
    int   history_count;
    int   history_cursor;

    FileMetadata selected;

    int   busy;
};

struct _AppStateClass {
    GObjectClass parent_class;
};

/* Create a new, initialised state object. The caller owns the reference. */
AppState *app_state_new(void);

void app_state_init(AppState *state);
void app_state_set_path(AppState *state, const char *path);
int  app_state_can_go_back(const AppState *state);
int  app_state_can_go_forward(const AppState *state);
FSStatus app_state_go_back(AppState *state);
FSStatus app_state_go_forward(AppState *state);
FSStatus app_state_go_up(AppState *state);

G_END_DECLS

#endif /* APP_STATE_H */
