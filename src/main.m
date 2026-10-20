#import <Foundation/Foundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "telemetry.h"
#include "theme.h"
#include "ui.h"

static struct termios s_orig_termios;
static bool s_raw_mode_active = false;
static volatile sig_atomic_t s_running = 1;
static volatile sig_atomic_t s_resized = 0;

static void disable_raw_mode(void) {
    if (s_raw_mode_active) {
        tcsetattr(STDIN_FILENO, TCSANOW, &s_orig_termios);
        s_raw_mode_active = false;
    }
}

static void enable_raw_mode(void) {
    if (tcgetattr(STDIN_FILENO, &s_orig_termios) == -1) return;
    struct termios raw = s_orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != -1) {
        s_raw_mode_active = true;
    }
}

static void sigint_handler(int sig) {
    (void)sig;
    s_running = 0;
}

static void sigwinch_handler(int sig) {
    (void)sig;
    s_resized = 1;
}

static void cleanup_and_exit(int code) {
    ui_cleanup();
    disable_raw_mode();
    exit(code);
}

static void save_user_config(const char *theme_name, int refresh_ms, ViewMode view_mode) {
    const char *home = getenv("HOME");
    if (!home) return;
    char config_root[512], dir[512], file[512];
    snprintf(config_root, sizeof(config_root), "%s/.config", home);
    snprintf(dir, sizeof(dir), "%s/.config/macpulse", home);
    snprintf(file, sizeof(file), "%s/.config/macpulse/config.json", home);
    mkdir(config_root, 0755);
    mkdir(dir, 0755);

    const char *v_str = "full";
    switch (view_mode) {
        case VIEW_FULL:     v_str = "full"; break;
        case VIEW_COMPACT:  v_str = "compact"; break;
        case VIEW_ZEN:      v_str = "zen"; break;
        case VIEW_OVERVIEW: v_str = "overview"; break;
        case VIEW_CPU:      v_str = "cpu"; break;
        case VIEW_GPU:      v_str = "gpu"; break;
        case VIEW_MEM:      v_str = "mem"; break;
        case VIEW_NET:      v_str = "net"; break;
        case VIEW_BENTO:    v_str = "bento"; break;
        case VIEW_RAIL:     v_str = "rail"; break;
        default:            v_str = "full"; break;
    }

    FILE *f = fopen(file, "w");
    if (!f) return;
    fprintf(f, "{\n  \"theme\": \"%s\",\n  \"refresh_rate_seconds\": %.2f,\n  \"view_mode\": \"%s\"\n}\n",
            theme_name, (float)refresh_ms / 1000.0f, v_str);
    fclose(f);
}

static void load_user_config(char *theme_out, size_t theme_sz, int *refresh_ms_out, ViewMode *view_mode_out) {
    const char *home = getenv("HOME");
    if (!home) return;
    char file[512];
    snprintf(file, sizeof(file), "%s/.config/macpulse/config.json", home);

    FILE *f = fopen(file, "r");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 8192) { fclose(f); return; }

    char *buf = malloc(sz + 1);
    if (!buf) { fclose(f); return; }
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);

    const char *t_pos = strstr(buf, "\"theme\"");
    if (t_pos) {
        t_pos = strchr(t_pos, ':');
        if (t_pos) {
            t_pos = strchr(t_pos, '\"');
            if (t_pos) {
                t_pos++;
                const char *end = strchr(t_pos, '\"');
                if (end) {
                    size_t len = end - t_pos;
                    if (len >= theme_sz) len = theme_sz - 1;
                    strncpy(theme_out, t_pos, len);
                    theme_out[len] = '\0';
                }
            }
        }
    }

    const char *r_pos = strstr(buf, "\"refresh_rate_seconds\"");
    if (r_pos) {
        r_pos = strchr(r_pos, ':');
        if (r_pos) {
            float r = atof(r_pos + 1);
            if (r >= 0.2f && r <= 10.0f) {
                *refresh_ms_out = (int)(r * 1000.0f);
            }
        }
    }

    const char *v_pos = strstr(buf, "\"view_mode\"");
    if (v_pos && view_mode_out) {
        v_pos = strchr(v_pos, ':');
        if (v_pos) {
            v_pos = strchr(v_pos, '\"');
            if (v_pos) {
                v_pos++;
                const char *end = strchr(v_pos, '\"');
                if (end) {
                    char v_buf[32];
                    size_t len = end - v_pos;
                    if (len >= sizeof(v_buf)) len = sizeof(v_buf) - 1;
                    strncpy(v_buf, v_pos, len);
                    v_buf[len] = '\0';
                    *view_mode_out = ui_view_mode_from_string(v_buf);
                }
            }
        }
    }
    free(buf);
}

static void print_usage(const char *prog) {
    printf("MacPulse - Modern Apple Silicon & macOS System Monitor & Dashboard for Terminal\n\n");
    printf("Usage: %s [OPTIONS]\n\n", prog);
    printf("Options:\n");
    printf("  -t, --theme <name>     Select color theme on launch (e.g. matrix, cyberpunk, dracula, nord)\n");
    printf("  -r, --rate <seconds>   Set refresh rate in seconds (default: 1.0, min: 0.2)\n");
    printf("  -V, --view, --layout   Set dashboard layout (full, compact, zen, overview, cpu, gpu, mem, net, bento, rail)\n");
    printf("  -l, --list-themes      List all available built-in and custom themes (15 themes)\n");
    printf("  -h, --help             Show this help message\n");
    printf("  -v, --version          Show version information\n\n");
    printf("Shortcuts inside MacPulse:\n");
    printf("  q                      Quit MacPulse\n");
    printf("  t                      Open interactive Theme Customizer popup\n");
    printf("  T                      Instantly cycle to next theme\n");
    printf("  g                      Cycle Graph modes (Split CPU/GPU, Stacked, Dual-Series, Network)\n");
    printf("  Tab                    Toggle Process Table / Maximize Graphs\n");
    printf("  v                      Cycle all 10 full-screen dashboard layouts\n");
    printf("  c / m / p / n          Sort processes by CPU%% / Memory / PID / Name\n");
    printf("  r                      Reverse sort order\n");
    printf("  /                      Search / filter processes by name or user\n");
    printf("  x                      Kill (SIGTERM) highlighted process\n");
    printf("  + / -                  Speed up / slow down refresh rate\n");
    printf("  h / ?                  Toggle Help modal\n");
}

int main(int argc, char *argv[]) {
    theme_init_defaults();
    theme_load_user_themes();

    int refresh_ms = 1000;
    char saved_theme[64] = "";
    ViewMode active_view_mode = VIEW_FULL;
    load_user_config(saved_theme, sizeof(saved_theme), &refresh_ms, &active_view_mode);

    if (saved_theme[0]) {
        theme_set_by_name(saved_theme);
    }

    const char *initial_theme = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("MacPulse v1.0.0 (Apple Silicon & macOS Native)\n");
            return 0;
        } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list-themes") == 0) {
            printf("Available MacPulse Themes (%d total):\n\n", theme_get_count());
            for (int j = 0; j < theme_get_count(); j++) {
                const Theme *th = theme_get(j);
                printf("  %2d. %-20s - %s\n", j + 1, th->name, th->desc);
            }
            return 0;
        } else if ((strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--theme") == 0) && i + 1 < argc) {
            initial_theme = argv[++i];
        } else if ((strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--rate") == 0) && i + 1 < argc) {
            float r = atof(argv[++i]);
            if (r >= 0.2f && r <= 10.0f) {
                refresh_ms = (int)(r * 1000.0f);
            }
        } else if ((strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--view") == 0 || strcmp(argv[i], "--layout") == 0) && i + 1 < argc) {
            active_view_mode = ui_view_mode_from_string(argv[++i]);
        }
    }

    if (initial_theme) {
        if (!theme_set_by_name(initial_theme)) {
            int idx = atoi(initial_theme) - 1;
            if (idx >= 0 && idx < theme_get_count()) {
                theme_set_by_index(idx);
            }
        }
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    struct sigaction sa_winch;
    memset(&sa_winch, 0, sizeof(sa_winch));
    sa_winch.sa_handler = sigwinch_handler;
    sigaction(SIGWINCH, &sa_winch, NULL);

    enable_raw_mode();
    ui_init();
    telemetry_init();

    UIState state;
    memset(&state, 0, sizeof(state));
    state.refresh_ms = refresh_ms;
    state.modal = MODAL_NONE;
    state.view_mode = active_view_mode;
    state.graph_mode = GRAPH_MODE_SPLIT;
    state.show_procs = true;

    int current_sort = SORT_BY_CPU;
    bool sort_reverse = false;
    if (state.view_mode == VIEW_MEM) {
        current_sort = SORT_BY_MEM;
        telemetry_sort_processes(current_sort, sort_reverse);
    }

    uint64_t last_update_ms = 0;

    struct timeval tv;
    gettimeofday(&tv, NULL);
    last_update_ms = (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;

    while (s_running) {
        gettimeofday(&tv, NULL);
        uint64_t now_ms = (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;

        if (now_ms - last_update_ms >= (uint64_t)state.refresh_ms || s_resized) {
            telemetry_update();
            ui_render(&state, telemetry_get(), theme_current());
            last_update_ms = now_ms;
            s_resized = 0;
        }

        // Wait for keyboard input with poll timeout
        int timeout = (int)(last_update_ms + state.refresh_ms - now_ms);
        if (timeout < 10) timeout = 10;
        if (timeout > state.refresh_ms) timeout = state.refresh_ms;

        struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN, .revents = 0 };
        int ret = poll(&pfd, 1, timeout);

        if (ret > 0 && (pfd.revents & POLLIN)) {
            char ch = 0;
            if (read(STDIN_FILENO, &ch, 1) > 0) {
                // Check escape sequence
                if (ch == 27) { // ESC or arrow key sequence
                    char seq[8] = {0};
                    if (read(STDIN_FILENO, &seq[0], 1) > 0) {
                        if (seq[0] == '[') {
                            if (read(STDIN_FILENO, &seq[1], 1) > 0) {
                                if (seq[1] == 'A') { // Up arrow
                                    if (state.modal == MODAL_THEME) {
                                        if (state.theme_cursor > 0) state.theme_cursor--;
                                    } else {
                                        if (state.proc_selected > 0) state.proc_selected--;
                                    }
                                } else if (seq[1] == 'B') { // Down arrow
                                    if (state.modal == MODAL_THEME) {
                                        if (state.theme_cursor < theme_get_count() - 1) state.theme_cursor++;
                                    } else {
                                        const SystemTelemetry *t_ptr = telemetry_get();
                                        if (state.proc_selected < t_ptr->procs.filtered_count - 1) state.proc_selected++;
                                    }
                                } else if (seq[1] == '5' && read(STDIN_FILENO, &seq[2], 1) > 0 && seq[2] == '~') { // Page Up
                                    state.proc_selected = (state.proc_selected > 10) ? state.proc_selected - 10 : 0;
                                } else if (seq[1] == '6' && read(STDIN_FILENO, &seq[2], 1) > 0 && seq[2] == '~') { // Page Down
                                    const SystemTelemetry *t_ptr = telemetry_get();
                                    state.proc_selected = (state.proc_selected + 10 < t_ptr->procs.filtered_count) ? state.proc_selected + 10 : t_ptr->procs.filtered_count - 1;
                                }
                            }
                        }
                    } else {
                        // Bare ESC pressed
                        if (state.modal != MODAL_NONE) {
                            state.modal = MODAL_NONE;
                        } else if (state.searching) {
                            state.searching = false;
                            state.search_buf[0] = '\0';
                            telemetry_filter_processes("");
                        }
                    }
                } else if (state.modal == MODAL_THEME) {
                    if (ch == '\n' || ch == '\r') {
                        theme_set_by_index(state.theme_cursor);
                        save_user_config(theme_current()->name, state.refresh_ms, state.view_mode);
                        state.modal = MODAL_NONE;
                    } else if (ch == 'q' || ch == 't') {
                        state.modal = MODAL_NONE;
                    } else if (ch == 'j') {
                        if (state.theme_cursor < theme_get_count() - 1) state.theme_cursor++;
                    } else if (ch == 'k') {
                        if (state.theme_cursor > 0) state.theme_cursor--;
                    }
                } else if (state.modal == MODAL_HELP) {
                    state.modal = MODAL_NONE;
                } else if (state.modal == MODAL_KILL_CONFIRM) {
                    if (ch == 'y' || ch == 'Y') {
                        if (state.target_kill_pid > 0) {
                            kill(state.target_kill_pid, SIGTERM);
                            char k_msg[128];
                            snprintf(k_msg, sizeof(k_msg), "Sent SIGTERM to PID %d (%s)", state.target_kill_pid, state.target_kill_name);
                            ui_set_status(&state, k_msg);
                        }
                        state.modal = MODAL_NONE;
                    } else {
                        state.modal = MODAL_NONE;
                    }
                } else if (state.searching) {
                    if (ch == '\n' || ch == '\r') {
                        state.searching = false;
                    } else if (ch == 127 || ch == 8) { // Backspace
                        size_t slen = strlen(state.search_buf);
                        if (slen > 0) {
                            state.search_buf[slen - 1] = '\0';
                            telemetry_filter_processes(state.search_buf);
                        }
                    } else if (ch >= 32 && ch <= 126) {
                        size_t slen = strlen(state.search_buf);
                        if (slen < sizeof(state.search_buf) - 1) {
                            state.search_buf[slen] = ch;
                            state.search_buf[slen + 1] = '\0';
                            telemetry_filter_processes(state.search_buf);
                        }
                    }
                } else {
                    // Standard normal mode keys
                    switch (ch) {
                        case 'q':
                        case 'Q':
                            s_running = 0;
                            break;
                        case 't':
                            state.modal = MODAL_THEME;
                            state.theme_cursor = theme_current_index();
                            break;
                        case 'T':
                            theme_cycle_next();
                            save_user_config(theme_current()->name, state.refresh_ms, state.view_mode);
                            break;
                        case 'g':
                        case 'G': {
                            state.graph_mode = (state.graph_mode + 1) % 4;
                            const char *g_names[] = {
                                "Graph: Split CPU & GPU (Side-by-side)",
                                "Graph: Stacked Full-Width (CPU / GPU)",
                                "Graph: Combined Dual-Series (CPU + GPU)",
                                "Graph: Network Bandwidth Throughput"
                            };
                            ui_set_status(&state, g_names[state.graph_mode]);
                            break;
                        }
                        case '\t':
                            state.show_procs = !state.show_procs;
                            ui_set_status(&state, state.show_procs ? "Activity Monitor Shown" : "Maximized Graphical History");
                            break;
                        case 'v':
                        case 'V': {
                            state.view_mode = (ViewMode)((state.view_mode + 1) % VIEW_COUNT);
                            char v_msg[128];
                            snprintf(v_msg, sizeof(v_msg), "View: %s", ui_view_mode_name(state.view_mode));
                            ui_set_status(&state, v_msg);

                            // Auto-sort intelligently for focused views
                            if (state.view_mode == VIEW_CPU && current_sort != SORT_BY_CPU) {
                                current_sort = SORT_BY_CPU;
                                sort_reverse = false;
                                telemetry_sort_processes(current_sort, sort_reverse);
                            } else if (state.view_mode == VIEW_MEM && current_sort != SORT_BY_MEM) {
                                current_sort = SORT_BY_MEM;
                                sort_reverse = false;
                                telemetry_sort_processes(current_sort, sort_reverse);
                            }

                            save_user_config(theme_current()->name, state.refresh_ms, state.view_mode);
                            break;
                        }
                        case 'c':
                        case 'C':
                            current_sort = SORT_BY_CPU;
                            telemetry_sort_processes(current_sort, sort_reverse);
                            ui_set_status(&state, "Sorted by %CPU");
                            break;
                        case 'm':
                        case 'M':
                            current_sort = SORT_BY_MEM;
                            telemetry_sort_processes(current_sort, sort_reverse);
                            ui_set_status(&state, "Sorted by Memory");
                            break;
                        case 'p':
                        case 'P':
                            current_sort = SORT_BY_PID;
                            telemetry_sort_processes(current_sort, sort_reverse);
                            ui_set_status(&state, "Sorted by PID");
                            break;
                        case 'n':
                        case 'N':
                            current_sort = SORT_BY_NAME;
                            telemetry_sort_processes(current_sort, sort_reverse);
                            ui_set_status(&state, "Sorted by Name");
                            break;
                        case 'r':
                        case 'R':
                            sort_reverse = !sort_reverse;
                            telemetry_sort_processes(current_sort, sort_reverse);
                            ui_set_status(&state, sort_reverse ? "Sort Reversed (Ascending)" : "Sort Normal (Descending)");
                            break;
                        case '/':
                            state.searching = true;
                            state.search_buf[0] = '\0';
                            break;
                        case 'x':
                        case 'K': {
                            const SystemTelemetry *t_ptr = telemetry_get();
                            if (state.proc_selected < t_ptr->procs.filtered_count) {
                                int p_idx = t_ptr->procs.filtered_indices[state.proc_selected];
                                state.target_kill_pid = t_ptr->procs.items[p_idx].pid;
                                strncpy(state.target_kill_name, t_ptr->procs.items[p_idx].name, sizeof(state.target_kill_name) - 1);
                                state.modal = MODAL_KILL_CONFIRM;
                            }
                            break;
                        }
                        case 'h':
                        case '?':
                            state.modal = MODAL_HELP;
                            break;
                        case '+':
                        case '=':
                            if (state.refresh_ms > 250) state.refresh_ms -= 250;
                            save_user_config(theme_current()->name, state.refresh_ms, state.view_mode);
                            break;
                        case '-':
                        case '_':
                            if (state.refresh_ms < 5000) state.refresh_ms += 250;
                            save_user_config(theme_current()->name, state.refresh_ms, state.view_mode);
                            break;
                        case 'j': {
                            const SystemTelemetry *t_ptr = telemetry_get();
                            if (state.proc_selected < t_ptr->procs.filtered_count - 1) state.proc_selected++;
                            break;
                        }
                        case 'k':
                            if (state.proc_selected > 0) state.proc_selected--;
                            break;
                    }
                }

                // Render immediately on user input
                ui_render(&state, telemetry_get(), theme_current());
            }
        }
    }

    cleanup_and_exit(0);
    return 0;
}
