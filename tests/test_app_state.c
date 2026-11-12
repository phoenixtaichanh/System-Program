/* ============================================================================
 * test_app_state.c -- Tests for the navigation history.
 * ============================================================================
 */

#define _XOPEN_SOURCE 700

#include "app_state.h"
#include "fs_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond) do { \
    if (cond) { g_pass++; } \
    else { \
        g_fail++; \
        fprintf(stderr, "  FAIL line %d: %s\n", __LINE__, #cond); \
    } \
} while (0)

#define CHECK_STR_EQ(actual, expected) do { \
    if (strcmp((actual), (expected)) == 0) { g_pass++; } \
    else { \
        g_fail++; \
        fprintf(stderr, "  FAIL line %d: \"%s\" != \"%s\"\n", \
                __LINE__, (actual), (expected)); \
    } \
} while (0)

/* ------------------------------------------------------------------ *
 * test_init
 * ------------------------------------------------------------------ */
static void
test_init(void)
{
    AppState *state = app_state_new();
    CHECK(state != NULL);
    if (state == NULL) return;

    CHECK(state->current_path[0] == '\0');
    CHECK(state->history_count == 0);
    CHECK(state->history_cursor == -1);
    CHECK(state->busy == 0);

    g_object_unref(state);
}

/* ------------------------------------------------------------------ *
 * test_set_path
 * ------------------------------------------------------------------ */
static void
test_set_path(void)
{
    AppState *state = app_state_new();
    if (state == NULL) return;

    app_state_set_path(state, "/home");
    CHECK_STR_EQ(state->current_path, "/home");
    CHECK(state->history_count == 1);
    CHECK(state->history_cursor == 0);

    app_state_set_path(state, "/etc");
    CHECK_STR_EQ(state->current_path, "/etc");
    CHECK(state->history_count == 2);
    CHECK(state->history_cursor == 1);

    /* Setting the same path again must not add a history entry. */
    app_state_set_path(state, "/etc");
    CHECK(state->history_count == 2);

    g_object_unref(state);
}

/* ------------------------------------------------------------------ *
 * test_back_forward
 * ------------------------------------------------------------------ */
static void
test_back_forward(void)
{
    AppState *state = app_state_new();
    if (state == NULL) return;

    app_state_set_path(state, "/a");
    app_state_set_path(state, "/b");
    app_state_set_path(state, "/c");

    /* With three entries we can go back twice but not forwards. */
    CHECK(app_state_can_go_back(state) == 1);
    CHECK(app_state_can_go_forward(state) == 0);

    CHECK(app_state_go_back(state) == FS_OK);
    CHECK_STR_EQ(state->current_path, "/b");

    CHECK(app_state_go_back(state) == FS_OK);
    CHECK_STR_EQ(state->current_path, "/a");

    /* Already at the oldest entry. */
    CHECK(app_state_can_go_back(state) == 0);
    CHECK(app_state_go_back(state) != FS_OK);

    CHECK(app_state_can_go_forward(state) == 1);
    CHECK(app_state_go_forward(state) == FS_OK);
    CHECK_STR_EQ(state->current_path, "/b");

    CHECK(app_state_go_forward(state) == FS_OK);
    CHECK_STR_EQ(state->current_path, "/c");

    CHECK(app_state_can_go_forward(state) == 0);
    CHECK(app_state_go_forward(state) != FS_OK);

    g_object_unref(state);
}

/* ------------------------------------------------------------------ *
 * test_forward_history_discarded
 *
 * Navigating to a new location after going back must discard the
 * forward history, exactly like a browser.
 * ------------------------------------------------------------------ */
static void
test_forward_history_discarded(void)
{
    AppState *state = app_state_new();
    if (state == NULL) return;

    app_state_set_path(state, "/a");
    app_state_set_path(state, "/b");
    app_state_set_path(state, "/c");

    app_state_go_back(state);      /* -> /b */
    CHECK_STR_EQ(state->current_path, "/b");

    app_state_set_path(state, "/d");   /* /c is dropped */
    CHECK_STR_EQ(state->current_path, "/d");
    CHECK(state->history_count == 3);
    CHECK(app_state_can_go_forward(state) == 0);

    g_object_unref(state);
}

/* ------------------------------------------------------------------ *
 * test_go_up
 * ------------------------------------------------------------------ */
static void
test_go_up(void)
{
    AppState *state = app_state_new();
    if (state == NULL) return;

    app_state_set_path(state, "/home/user/docs");
    CHECK(app_state_go_up(state) == FS_OK);
    CHECK_STR_EQ(state->current_path, "/home/user");

    CHECK(app_state_go_up(state) == FS_OK);
    CHECK_STR_EQ(state->current_path, "/home");

    /* Going up from the root stays at the root. */
    app_state_set_path(state, "/");
    CHECK(app_state_go_up(state) == FS_OK);
    CHECK_STR_EQ(state->current_path, "/");

    g_object_unref(state);
}

/* ------------------------------------------------------------------ *
 * test_history_overflow
 *
 * The history is bounded: the oldest entries are dropped.
 * ------------------------------------------------------------------ */
static void
test_history_overflow(void)
{
    AppState *state = app_state_new();
    if (state == NULL) return;

    /* The loop visits HISTORY_MAX + 20 paths, so the last one is
     * "/dir83" (the loop index, not a fixed literal). */
    for (int i = 0; i < HISTORY_MAX + 20; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dir%d", i);
        app_state_set_path(state, path);
    }

    CHECK(state->history_count <= HISTORY_MAX);
    CHECK_STR_EQ(state->current_path, "/dir83");

    /* The oldest entries must have been dropped: the history starts at
     * the path that was visited when the list became full. */
    CHECK(state->history[0] != NULL);
    CHECK(strcmp(state->history[0], "/dir20") == 0);

    /* The cursor points at the newest entry. */
    CHECK(state->history_cursor == state->history_count - 1);
    CHECK_STR_EQ(state->history[state->history_cursor], "/dir83");

    g_object_unref(state);
}

/* ------------------------------------------------------------------ *
 * test_selected
 * ------------------------------------------------------------------ */
static void
test_selected(void)
{
    AppState *state = app_state_new();
    if (state == NULL) return;

    CHECK(state->selected.name[0] == '\0');

    snprintf(state->selected.name, sizeof(state->selected.name),
             "file.txt");
    CHECK_STR_EQ(state->selected.name, "file.txt");

    g_object_unref(state);
}

/* ------------------------------------------------------------------ *
 * main
 * ------------------------------------------------------------------ */
int
main(void)
{
    printf("==== app_state test ====\n");

    test_init();
    test_set_path();
    test_back_forward();
    test_forward_history_discarded();
    test_go_up();
    test_history_overflow();
    test_selected();

    printf("==== Result: %d passed, %d failed ====\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
