/* ============================================================================
 * app_state.h -- Shared application state.
 * Implementation lands in PHASE 9.
 * ============================================================================
 */

#ifndef APP_STATE_H
#define APP_STATE_H

#include "fs_types.h"

#define HISTORY_MAX 64

typedef struct {
    char  current_path[FS_PATH_MAX];
    char  history[HISTORY_MAX][FS_PATH_MAX];
    int   history_count;
    int   history_cursor;

    FileMetadata selected;

    int   busy;
} AppState;

void app_state_init(AppState *state);
void app_state_set_path(AppState *state, const char *path);
int  app_state_can_go_back(const AppState *state);
int  app_state_can_go_forward(const AppState *state);
FSStatus app_state_go_back(AppState *state);
FSStatus app_state_go_forward(AppState *state);
FSStatus app_state_go_up(AppState *state);

#endif /* APP_STATE_H */