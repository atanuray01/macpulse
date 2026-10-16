#ifndef MACPULSE_UI_H
#define MACPULSE_UI_H

#include <stdbool.h>
#include <sys/types.h>
#include "telemetry.h"
#include "theme.h"

typedef enum {
    MODAL_NONE = 0,
    MODAL_THEME,
    MODAL_HELP,
    MODAL_KILL_CONFIRM
} ModalType;

typedef enum {
    VIEW_FULL = 0,
    VIEW_COMPACT,
    VIEW_ZEN
} ViewMode;

typedef struct {
    int term_cols;
    int term_rows;
    ModalType modal;
    int theme_cursor;
    ViewMode view_mode;

    // Process selection & scrolling
    int proc_scroll;
    int proc_selected;
    char search_buf[64];
    bool searching;

    // Refresh rate in milliseconds
    int refresh_ms;

    // Status message (e.g. "Signal sent to PID 1234")
    char status_msg[128];
    time_t status_msg_time;

    // Process to kill
    pid_t target_kill_pid;
    char target_kill_name[64];
} UIState;

void ui_init(void);
void ui_cleanup(void);
void ui_handle_resize(void);
void ui_render(UIState *state, const SystemTelemetry *telem, const Theme *theme);
void ui_set_status(UIState *state, const char *msg);

#endif // MACPULSE_UI_H
