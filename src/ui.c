#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <time.h>
#include <math.h>

#define MAX_GRID_COLS 320
#define MAX_GRID_ROWS 160

typedef struct {
    char ch[8];     // Multi-byte UTF-8 or single ASCII
    ColorRGB fg;
    ColorRGB bg;
    bool has_bg;
    bool bold;
    bool dim;
} Cell;

typedef struct {
    int cols;
    int rows;
    Cell cells[MAX_GRID_ROWS][MAX_GRID_COLS];
} ScreenGrid;

static ScreenGrid s_grid;

// Dynamic string buffer for atomic output flush
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

static StrBuf s_out_buf = {NULL, 0, 0};

static void buf_init(StrBuf *b, size_t initial_cap) {
    b->data = malloc(initial_cap);
    b->len = 0;
    b->cap = initial_cap;
    if (b->data) b->data[0] = '\0';
}

static void buf_reset(StrBuf *b) {
    b->len = 0;
    if (b->data) b->data[0] = '\0';
}

static void buf_free(StrBuf *b) {
    if (b->data) {
        free(b->data);
        b->data = NULL;
    }
    b->len = 0;
    b->cap = 0;
}

static void buf_ensure(StrBuf *b, size_t add_len) {
    if (b->len + add_len + 1 > b->cap) {
        size_t new_cap = (b->cap * 2 > b->len + add_len + 4096) ? b->cap * 2 : (b->len + add_len + 4096);
        b->data = realloc(b->data, new_cap);
        b->cap = new_cap;
    }
}

static void buf_append_str(StrBuf *b, const char *s) {
    if (!s) return;
    size_t slen = strlen(s);
    buf_ensure(b, slen);
    memcpy(b->data + b->len, s, slen);
    b->len += slen;
    b->data[b->len] = '\0';
}

static void buf_printf(StrBuf *b, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char tmp[1024];
    int n = vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    if (n > 0) {
        buf_append_str(b, tmp);
    }
}

void ui_init(void) {
    buf_init(&s_out_buf, 65536);
    // Enter alternate screen, hide cursor, clear
    write(STDOUT_FILENO, "\033[?1049h\033[2J\033[H\033[?25l", 18);
}

void ui_cleanup(void) {
    // Leave alternate screen, show cursor
    write(STDOUT_FILENO, "\033[?1049l\033[?25h", 12);
    buf_free(&s_out_buf);
}

void ui_handle_resize(void) {
    // Detected dynamically during render
}

void ui_set_status(UIState *state, const char *msg) {
    if (msg) {
        strncpy(state->status_msg, msg, sizeof(state->status_msg) - 1);
        state->status_msg_time = time(NULL);
    } else {
        state->status_msg[0] = '\0';
    }
}

// -------------------------------------------------------------
// Screen Grid primitives
// -------------------------------------------------------------
static void grid_reset(int cols, int rows, const Theme *t) {
    if (cols > MAX_GRID_COLS) cols = MAX_GRID_COLS;
    if (rows > MAX_GRID_ROWS) rows = MAX_GRID_ROWS;
    s_grid.cols = cols;
    s_grid.rows = rows;

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            Cell *cell = &s_grid.cells[r][c];
            strcpy(cell->ch, " ");
            cell->fg = t->text;
            cell->bg = t->header_bg;
            cell->has_bg = false;
            cell->bold = false;
            cell->dim = false;
        }
    }
}

static void grid_set(int c, int r, const char *utf8_char, ColorRGB fg, ColorRGB bg, bool has_bg, bool bold, bool dim) {
    if (c < 0 || c >= s_grid.cols || r < 0 || r >= s_grid.rows) return;
    Cell *cell = &s_grid.cells[r][c];
    if (utf8_char && utf8_char[0]) {
        strncpy(cell->ch, utf8_char, sizeof(cell->ch) - 1);
        cell->ch[sizeof(cell->ch) - 1] = '\0';
    } else {
        strcpy(cell->ch, " ");
    }
    cell->fg = fg;
    cell->bg = bg;
    cell->has_bg = has_bg;
    cell->bold = bold;
    cell->dim = dim;
}

static int grid_print(int c, int r, int max_w, const char *text, ColorRGB fg, bool bold, bool dim) {
    if (r < 0 || r >= s_grid.rows || c >= s_grid.cols || !text) return 0;
    int written = 0;
    const unsigned char *p = (const unsigned char *)text;

    while (*p && (max_w <= 0 || written < max_w) && (c + written) < s_grid.cols) {
        char utf8_ch[8] = {0};
        int ch_len = 1;

        if (*p < 0x80) {
            utf8_ch[0] = *p;
            ch_len = 1;
        } else if ((*p & 0xE0) == 0xC0) {
            utf8_ch[0] = p[0]; utf8_ch[1] = p[1];
            ch_len = 2;
        } else if ((*p & 0xF0) == 0xE0) {
            utf8_ch[0] = p[0]; utf8_ch[1] = p[1]; utf8_ch[2] = p[2];
            ch_len = 3;
        } else if ((*p & 0xF8) == 0xF0) {
            utf8_ch[0] = p[0]; utf8_ch[1] = p[1]; utf8_ch[2] = p[2]; utf8_ch[3] = p[3];
            ch_len = 4;
        }

        grid_set(c + written, r, utf8_ch, fg, (ColorRGB){0,0,0}, false, bold, dim);
        written++;
        p += ch_len;
    }
    return written;
}

static void grid_print_bg(int c, int r, int max_w, const char *text, ColorRGB fg, ColorRGB bg, bool bold) {
    if (r < 0 || r >= s_grid.rows || c >= s_grid.cols || !text) return;
    int written = 0;
    const unsigned char *p = (const unsigned char *)text;

    while (*p && (max_w <= 0 || written < max_w) && (c + written) < s_grid.cols) {
        char utf8_ch[8] = {0};
        int ch_len = 1;

        if (*p < 0x80) {
            utf8_ch[0] = *p;
            ch_len = 1;
        } else if ((*p & 0xE0) == 0xC0) {
            utf8_ch[0] = p[0]; utf8_ch[1] = p[1];
            ch_len = 2;
        } else if ((*p & 0xF0) == 0xE0) {
            utf8_ch[0] = p[0]; utf8_ch[1] = p[1]; utf8_ch[2] = p[2];
            ch_len = 3;
        } else if ((*p & 0xF8) == 0xF0) {
            utf8_ch[0] = p[0]; utf8_ch[1] = p[1]; utf8_ch[2] = p[2]; utf8_ch[3] = p[3];
            ch_len = 4;
        }

        grid_set(c + written, r, utf8_ch, fg, bg, true, bold, false);
        written++;
        p += ch_len;
    }
}

// Draw framed box with clean hacker border
static void grid_box(int x, int y, int w, int h, const char *title, const char *extra,
                     ColorRGB border_col, ColorRGB title_col, ColorRGB extra_col) {
    if (w < 4 || h < 2) return;

    // Corners & horizontal edges
    grid_set(x, y, "┌", border_col, (ColorRGB){0,0,0}, false, false, false);
    grid_set(x + w - 1, y, "┐", border_col, (ColorRGB){0,0,0}, false, false, false);
    grid_set(x, y + h - 1, "└", border_col, (ColorRGB){0,0,0}, false, false, false);
    grid_set(x + w - 1, y + h - 1, "┘", border_col, (ColorRGB){0,0,0}, false, false, false);

    for (int c = x + 1; c < x + w - 1; c++) {
        grid_set(c, y, "─", border_col, (ColorRGB){0,0,0}, false, false, false);
        grid_set(c, y + h - 1, "─", border_col, (ColorRGB){0,0,0}, false, false, false);
    }

    // Vertical edges & clear inside
    for (int r = y + 1; r < y + h - 1; r++) {
        grid_set(x, r, "│", border_col, (ColorRGB){0,0,0}, false, false, false);
        grid_set(x + w - 1, r, "│", border_col, (ColorRGB){0,0,0}, false, false, false);
        for (int c = x + 1; c < x + w - 1; c++) {
            grid_set(c, r, " ", border_col, (ColorRGB){0,0,0}, false, false, false);
        }
    }

    // Embed title: ┌─ Title ───...──┐
    int cur_x = x + 2;
    if (title && title[0]) {
        grid_set(x + 1, y, "─", border_col, (ColorRGB){0,0,0}, false, false, false);
        int t_len = grid_print(cur_x, y, w - 6, title, title_col, true, false);
        cur_x += t_len;
        grid_set(cur_x++, y, " ", border_col, (ColorRGB){0,0,0}, false, false, false);
    }

    if (extra && extra[0] && (w - cur_x - (int)strlen(extra) - 2) > 0) {
        grid_print(cur_x, y, w - cur_x - 3, extra, extra_col, false, false);
    }
}

// Draw internal horizontal section divider: ├─ Title ────┤
static void grid_divider(int x, int y, int w, const char *title, const char *extra,
                         ColorRGB border_col, ColorRGB title_col, ColorRGB extra_col) {
    if (w < 4) return;
    grid_set(x, y, "├", border_col, (ColorRGB){0,0,0}, false, false, false);
    grid_set(x + w - 1, y, "┤", border_col, (ColorRGB){0,0,0}, false, false, false);

    for (int c = x + 1; c < x + w - 1; c++) {
        grid_set(c, y, "─", border_col, (ColorRGB){0,0,0}, false, false, false);
    }

    int cur_x = x + 2;
    if (title && title[0]) {
        grid_set(x + 1, y, "─", border_col, (ColorRGB){0,0,0}, false, false, false);
        int t_len = grid_print(cur_x, y, w - 6, title, title_col, true, false);
        cur_x += t_len;
        grid_set(cur_x++, y, " ", border_col, (ColorRGB){0,0,0}, false, false, false);
    }

    if (extra && extra[0] && (w - cur_x - (int)strlen(extra) - 2) > 0) {
        grid_print(cur_x, y, w - cur_x - 3, extra, extra_col, false, false);
    }
}

// Classic Linux / Hacker terminal bar gauge: [||||||||||||||||....................] 46%
static void grid_bar(int x, int y, int width, float pct,
                     ColorRGB fill_col, ColorRGB empty_col, ColorRGB text_col) {
    if (width < 6) return;
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;

    grid_set(x, y, "[", fill_col, (ColorRGB){0,0,0}, false, true, false);
    grid_set(x + width - 1, y, "]", fill_col, (ColorRGB){0,0,0}, false, true, false);

    int inner_w = width - 2;
    int filled = (int)((pct / 100.0f) * (float)inner_w);
    if (filled > inner_w) filled = inner_w;

    for (int i = 0; i < filled; i++) {
        grid_set(x + 1 + i, y, "|", fill_col, (ColorRGB){0,0,0}, false, true, false);
    }
    for (int i = filled; i < inner_w; i++) {
        grid_set(x + 1 + i, y, ".", empty_col, (ColorRGB){0,0,0}, false, false, true);
    }

    char pct_buf[16];
    snprintf(pct_buf, sizeof(pct_buf), "%3.0f%%", pct);
    grid_print(x + width + 1, y, 6, pct_buf, text_col, true, false);
}

// Draw sparkline graph inside panel
static void grid_sparkline(int x, int y, int width, int height,
                           const float *history, int count, float max_val,
                           ColorRGB low_col, ColorRGB high_col, ColorRGB dim_col) {
    const char *blocks[] = {" ", " ", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
    if (max_val <= 0.0f) max_val = 100.0f;
    if (width <= 0 || height <= 0) return;

    for (int r = 0; r < height; r++) {
        int row_y = y + r;
        float row_bottom = (float)(height - 1 - r) / (float)height * max_val;
        float row_top = (float)(height - r) / (float)height * max_val;
        float row_height = row_top - row_bottom;

        for (int c = 0; c < width; c++) {
            int hist_idx = count - width + c;
            float val = 0.0f;
            if (hist_idx >= 0 && hist_idx < count) {
                val = history[hist_idx];
            }

            if (val <= row_bottom) {
                grid_set(x + c, row_y, " ", dim_col, (ColorRGB){0,0,0}, false, false, false);
            } else if (val >= row_top) {
                float t = val / max_val;
                ColorRGB col = color_lerp(low_col, high_col, t);
                grid_set(x + c, row_y, "█", col, (ColorRGB){0,0,0}, false, false, false);
            } else {
                float fraction = (val - row_bottom) / row_height;
                int block_idx = (int)(fraction * 8.0f);
                if (block_idx < 1) block_idx = 1;
                if (block_idx > 8) block_idx = 8;
                float t = val / max_val;
                ColorRGB col = color_lerp(low_col, high_col, t);
                grid_set(x + c, row_y, blocks[block_idx], col, (ColorRGB){0,0,0}, false, false, false);
            }
        }
    }
}

static void format_bytes(uint64_t bytes, char *out, size_t max) {
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int u = 0;
    double d = (double)bytes;
    while (d >= 1024.0 && u < 4) {
        d /= 1024.0;
        u++;
    }
    if (u == 0) {
        snprintf(out, max, "%lluB", bytes);
    } else {
        snprintf(out, max, "%.1f%s", d, units[u]);
    }
}

static void format_speed(uint64_t bps, char *out, size_t max) {
    const char *units[] = {"B/s", "KB/s", "MB/s", "GB/s"};
    int u = 0;
    double d = (double)bps;
    while (d >= 1024.0 && u < 3) {
        d /= 1024.0;
        u++;
    }
    if (u == 0) {
        snprintf(out, max, "%3.0f%s", d, units[u]);
    } else {
        snprintf(out, max, "%5.1f%s", d, units[u]);
    }
}

static void format_time_sec(uint64_t sec, char *out, size_t max) {
    int m = (int)(sec / 60);
    int s = (int)(sec % 60);
    snprintf(out, max, "%dm%02ds", m, s);
}

// -------------------------------------------------------------
// Output serialization to stdout
// -------------------------------------------------------------
static void grid_flush(void) {
    buf_reset(&s_out_buf);
    buf_append_str(&s_out_buf, "\033[H"); // Cursor home

    ColorRGB prev_fg = {0,0,0};
    ColorRGB prev_bg = {0,0,0};
    bool prev_has_bg = false;
    bool prev_bold = false;
    bool prev_dim = false;
    bool in_style = false;

    for (int r = 0; r < s_grid.rows; r++) {
        for (int c = 0; c < s_grid.cols; c++) {
            Cell *cell = &s_grid.cells[r][c];

            bool fg_change = !in_style || memcmp(&cell->fg, &prev_fg, sizeof(ColorRGB)) != 0;
            bool bg_change = !in_style || (cell->has_bg != prev_has_bg) || (cell->has_bg && memcmp(&cell->bg, &prev_bg, sizeof(ColorRGB)) != 0);
            bool bold_change = !in_style || cell->bold != prev_bold;
            bool dim_change = !in_style || cell->dim != prev_dim;

            if (fg_change || bg_change || bold_change || dim_change) {
                buf_append_str(&s_out_buf, "\033[0m");
                char col_buf[64];
                ansi_fg_rgb(col_buf, sizeof(col_buf), cell->fg);
                buf_append_str(&s_out_buf, col_buf);

                if (cell->has_bg) {
                    ansi_bg_rgb(col_buf, sizeof(col_buf), cell->bg);
                    buf_append_str(&s_out_buf, col_buf);
                }
                if (cell->bold) buf_append_str(&s_out_buf, "\033[1m");
                if (cell->dim) buf_append_str(&s_out_buf, "\033[2m");

                prev_fg = cell->fg;
                prev_bg = cell->bg;
                prev_has_bg = cell->has_bg;
                prev_bold = cell->bold;
                prev_dim = cell->dim;
                in_style = true;
            }

            buf_append_str(&s_out_buf, cell->ch);
        }
        if (r < s_grid.rows - 1) {
            buf_append_str(&s_out_buf, "\033[0m\n");
            in_style = false;
        }
    }
    buf_append_str(&s_out_buf, "\033[0m");

    write(STDOUT_FILENO, s_out_buf.data, s_out_buf.len);
}

// -------------------------------------------------------------
// Main Render Pass
// -------------------------------------------------------------
void ui_render(UIState *state, const SystemTelemetry *telem, const Theme *t) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        state->term_cols = ws.ws_col;
        state->term_rows = ws.ws_row;
    } else {
        state->term_cols = 120;
        state->term_rows = 35;
    }

    int W = state->term_cols;
    int H = state->term_rows;
    grid_reset(W, H, t);

    // ==========================================
    // 1. TOP HEADER BAR
    // ==========================================
    char hdr_left[256];
    snprintf(hdr_left, sizeof(hdr_left), "macpulse • %s • %dC (%dE+%dP) • %d GPU • v1.0.0",
             telem->chip_name, telem->total_cores, telem->e_cores, telem->p_cores, telem->gpu_cores);

    time_t raw_time = time(NULL);
    struct tm *time_info = localtime(&raw_time);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", time_info);

    char hdr_right[256];
    if (telem->has_battery) {
        snprintf(hdr_right, sizeof(hdr_right), "⚡ %d%%%s • %s • Up: %s • %s",
                 telem->battery_pct, telem->battery_charging ? "[CHG]" : "",
                 telem->thermal_state_str, telem->uptime_str, time_str);
    } else {
        snprintf(hdr_right, sizeof(hdr_right), "🔌 AC • %s • Up: %s • %s",
                 telem->thermal_state_str, telem->uptime_str, time_str);
    }

    // Fill header row background
    for (int c = 0; c < W; c++) {
        grid_set(c, 0, " ", t->header_fg, t->header_bg, true, false, false);
    }
    grid_print_bg(1, 0, W - 2, hdr_left, t->header_fg, t->header_bg, true);
    int right_x = W - (int)strlen(hdr_right) - 1;
    if (right_x > (int)strlen(hdr_left) + 3) {
        grid_print_bg(right_x, 0, W - right_x - 1, hdr_right, t->header_fg, t->header_bg, false);
    }

    // ==========================================
    // 2. LAYOUT SPLIT (SIDEBAR LEFT + PROCESS RIGHT)
    // ==========================================
    int side_w = 46;
    if (W < 95) {
        side_w = W * 45 / 100;
        if (side_w < 38) side_w = 38;
    }
    if (side_w > W - 30) side_w = W - 30;

    int proc_x = side_w;
    int proc_w = W - side_w;
    int main_h = H - 2; // Rows 1 .. H-2
    int start_y = 1;

    // ------------------------------------------
    // LEFT SIDEBAR: HARDWARE TELEMETRY
    // ------------------------------------------
    grid_box(0, start_y, side_w, main_h, NULL, NULL, t->border, t->title, t->accent);

    int cur_y = start_y;

    // 1. GPU USAGE
    char gpu_title[64];
    snprintf(gpu_title, sizeof(gpu_title), "GPU Usage: %.0f%% (%.0f°C)", telem->gpu_device_pct, telem->gpu_temp_avg);
    grid_divider(0, cur_y, side_w, gpu_title, NULL, t->border, t->title, t->accent);
    cur_y++;

    // GPU Bar
    grid_bar(2, cur_y, side_w - 10, telem->gpu_device_pct, t->gpu_color, t->dim, t->text);
    cur_y++;

    // GPU History Graph (sparkline)
    int gpu_graph_h = 2;
    grid_sparkline(2, cur_y, side_w - 4, gpu_graph_h,
                   telem->history_gpu, telem->history_gpu_count, 100.0f,
                   t->gpu_color, t->crit, t->dim);
    cur_y += gpu_graph_h;

    // GPU VRAM
    char in_vram[32], alloc_vram[32];
    format_bytes(telem->gpu_in_use_bytes, in_vram, sizeof(in_vram));
    format_bytes(telem->gpu_alloc_bytes, alloc_vram, sizeof(alloc_vram));
    char vram_buf[64];
    snprintf(vram_buf, sizeof(vram_buf), "VRAM: %s / %s (Renderer: %.0f%%)", in_vram, alloc_vram, telem->gpu_renderer_pct);
    grid_print(2, cur_y, side_w - 4, vram_buf, t->dim, false, false);
    cur_y++;

    // 2. CPU USAGE
    char cpu_title[64];
    snprintf(cpu_title, sizeof(cpu_title), "CPU Usage: %dC (%dE/%dP) %.1f%% (%.0f°C)",
             telem->total_cores, telem->e_cores, telem->p_cores, telem->cpu_total_pct, telem->cpu_temp_avg);
    grid_divider(0, cur_y, side_w, cpu_title, NULL, t->border, t->title, t->accent);
    cur_y++;

    // CPU Bar
    grid_bar(2, cur_y, side_w - 10, telem->cpu_total_pct, t->cpu_color, t->dim, t->text);
    cur_y++;

    // E-Cores
    char e_buf[128] = "E-Cores: ";
    for (int i = 0; i < telem->e_cores && i < 6; i++) {
        char tmp[24];
        snprintf(tmp, sizeof(tmp), "E%d:%2.0f%% ", i, telem->core_pct[telem->p_cores + i]);
        strcat(e_buf, tmp);
    }
    grid_print(2, cur_y, side_w - 4, e_buf, t->dim, false, false);
    cur_y++;

    // P-Cores
    char p_buf[128] = "P-Cores: ";
    for (int i = 0; i < telem->p_cores && i < 6; i++) {
        char tmp[24];
        snprintf(tmp, sizeof(tmp), "P%d:%2.0f%% ", i, telem->core_pct[i]);
        strcat(p_buf, tmp);
    }
    grid_print(2, cur_y, side_w - 4, p_buf, t->dim, false, false);
    cur_y++;

    // CPU Sparkline Graph
    int cpu_graph_h = 2;
    grid_sparkline(2, cur_y, side_w - 4, cpu_graph_h,
                   telem->history_cpu, telem->history_cpu_count, 100.0f,
                   t->cpu_color, t->crit, t->dim);
    cur_y += cpu_graph_h;

    // Load average
    char load_buf[64];
    snprintf(load_buf, sizeof(load_buf), "Load: %.2f %.2f %.2f (Usr: %.1f%% Sys: %.1f%%)",
             telem->load_avg[0], telem->load_avg[1], telem->load_avg[2],
             telem->cpu_user_pct, telem->cpu_sys_pct);
    grid_print(2, cur_y, side_w - 4, load_buf, t->dim, false, false);
    cur_y++;

    // 3. UNIFIED MEMORY
    char mem_used_str[32], mem_tot_str[32];
    format_bytes(telem->mem_used_bytes, mem_used_str, sizeof(mem_used_str));
    format_bytes(telem->mem_total_bytes, mem_tot_str, sizeof(mem_tot_str));
    char mem_title[64];
    snprintf(mem_title, sizeof(mem_title), "Unified Memory: %s / %s", mem_used_str, mem_tot_str);
    grid_divider(0, cur_y, side_w, mem_title, NULL, t->border, t->title, t->accent);
    cur_y++;

    // RAM Bar
    grid_bar(2, cur_y, side_w - 10, telem->mem_used_pct, t->mem_color, t->dim, t->text);
    cur_y++;

    // App / Wired / Compressed
    char app_str[32], wired_str[32], comp_str[32];
    format_bytes(telem->mem_active_bytes, app_str, sizeof(app_str));
    format_bytes(telem->mem_wired_bytes, wired_str, sizeof(wired_str));
    format_bytes(telem->mem_compressed_bytes, comp_str, sizeof(comp_str));
    char mem_det1[64];
    snprintf(mem_det1, sizeof(mem_det1), "App: %s | Wired: %s | Comp: %s", app_str, wired_str, comp_str);
    grid_print(2, cur_y, side_w - 4, mem_det1, t->dim, false, false);
    cur_y++;

    // Cache & Free & Swap
    char cache_str[32], free_str[32], swap_str[32];
    format_bytes(telem->mem_inactive_bytes, cache_str, sizeof(cache_str));
    format_bytes(telem->mem_free_bytes, free_str, sizeof(free_str));
    format_bytes(telem->swap_used_bytes, swap_str, sizeof(swap_str));
    char mem_det2[64];
    snprintf(mem_det2, sizeof(mem_det2), "Cache: %s | Free: %s | Swap: %s", cache_str, free_str, swap_str);
    grid_print(2, cur_y, side_w - 4, mem_det2, t->dim, false, false);
    cur_y++;

    // 4. NETWORK & DISK
    grid_divider(0, cur_y, side_w, "Network & Disk", NULL, t->border, t->title, t->accent);
    cur_y++;

    // Net speeds
    char down_s[32], up_s[32];
    format_speed(telem->net_download_bps, down_s, sizeof(down_s));
    format_speed(telem->net_upload_bps, up_s, sizeof(up_s));
    char net_str[64];
    snprintf(net_str, sizeof(net_str), "Net:  ▼ %-9s ▲ %-9s", down_s, up_s);
    grid_print(2, cur_y, side_w - 4, net_str, t->accent, false, false);
    cur_y++;

    // Disk I/O
    char read_s[32], write_s[32];
    format_speed(telem->disk_read_bps, read_s, sizeof(read_s));
    format_speed(telem->disk_write_bps, write_s, sizeof(write_s));
    char disk_io[64];
    snprintf(disk_io, sizeof(disk_io), "Disk: R %-9s W %-9s", read_s, write_s);
    grid_print(2, cur_y, side_w - 4, disk_io, t->dim, false, false);
    cur_y++;

    // Storage HD
    char disk_u[32], disk_t[32], disk_f[32];
    format_bytes(telem->disk_used_bytes, disk_u, sizeof(disk_u));
    format_bytes(telem->disk_total_bytes, disk_t, sizeof(disk_t));
    format_bytes(telem->disk_free_bytes, disk_f, sizeof(disk_f));
    char hd_str[64];
    snprintf(hd_str, sizeof(hd_str), "Mac HD: %s/%s (%s free)", disk_u, disk_t, disk_f);
    grid_print(2, cur_y, side_w - 4, hd_str, t->dim, false, false);
    cur_y++;

    // 5. APPLE SILICON SPECS (if space permits)
    if (cur_y < start_y + main_h - 2) {
        grid_divider(0, cur_y, side_w, "Apple Silicon", NULL, t->border, t->title, t->accent);
        cur_y++;
        char soc_str[64];
        snprintf(soc_str, sizeof(soc_str), "%s (%d Cores, %d GPU Cores)", telem->chip_name, telem->total_cores, telem->gpu_cores);
        grid_print(2, cur_y, side_w - 4, soc_str, t->title, true, false);
        cur_y++;
        if (cur_y < start_y + main_h - 1) {
            char therm_str[64];
            snprintf(therm_str, sizeof(therm_str), "SoC Max: %.1f°C | SSD: %.1f°C | Batt: %.1f°C",
                     telem->soc_temp_max, telem->ssd_temp, telem->battery_temp);
            grid_print(2, cur_y, side_w - 4, therm_str, t->dim, false, false);
        }
    }

    // ------------------------------------------
    // RIGHT PANEL: PROCESS LIST / ACTIVITY MONITOR
    // ------------------------------------------
    char proc_title[64];
    if (state->search_buf[0]) {
        snprintf(proc_title, sizeof(proc_title), "Process List (%d/%d, /:\"%s\")",
                 telem->procs.filtered_count, telem->procs.count, state->search_buf);
    } else {
        snprintf(proc_title, sizeof(proc_title), "Process List (%d processes)", telem->procs.count);
    }
    grid_box(proc_x, start_y, proc_w, main_h, proc_title, "↑/↓ scroll, / search, x kill",
             t->border, t->title, t->dim);

    // Column Header
    int tbl_y = start_y + 1;
    char col_hdr[256];
    snprintf(col_hdr, sizeof(col_hdr), "%-6s  %-12s  %-8s  %5s  %5s  %5s  %-6s  %s",
             "PID", "USER", "RES", "%CPU", "%GPU", "%MEM", "TIME", "COMMAND");
    grid_print(proc_x + 2, tbl_y, proc_w - 4, col_hdr, t->accent, true, false);

    // Process rows
    int avail_rows = main_h - 3;
    if (avail_rows < 1) avail_rows = 1;

    if (state->proc_selected < 0) state->proc_selected = 0;
    if (state->proc_selected >= telem->procs.filtered_count) {
        state->proc_selected = telem->procs.filtered_count > 0 ? telem->procs.filtered_count - 1 : 0;
    }
    if (state->proc_selected < state->proc_scroll) {
        state->proc_scroll = state->proc_selected;
    }
    if (state->proc_selected >= state->proc_scroll + avail_rows) {
        state->proc_scroll = state->proc_selected - avail_rows + 1;
    }

    for (int r = 0; r < avail_rows; r++) {
        int idx = state->proc_scroll + r;
        int row_y = tbl_y + 1 + r;

        if (idx < telem->procs.filtered_count) {
            int p_idx = telem->procs.filtered_indices[idx];
            const ProcessEntry *pe = &telem->procs.items[p_idx];

            char res_str[16], time_s[16];
            format_bytes(pe->mem_bytes, res_str, sizeof(res_str));
            format_time_sec(pe->cpu_time_sec, time_s, sizeof(time_s));

            char row_line[512];
            snprintf(row_line, sizeof(row_line), "%-6d  %-12.12s  %-8s  %4.1f%%  %4.1f%%  %4.1f%%  %-6s  %s",
                     pe->pid, pe->username, res_str, pe->cpu_pct, pe->gpu_pct, pe->mem_pct, time_s, pe->name);

            bool is_sel = (idx == state->proc_selected);
            if (is_sel) {
                // Fill entire row with selection background
                for (int c = proc_x + 1; c < proc_x + proc_w - 1; c++) {
                    grid_set(c, row_y, " ", t->proc_sel_fg, t->proc_sel_bg, true, false, false);
                }
                grid_print_bg(proc_x + 2, row_y, proc_w - 4, row_line, t->proc_sel_fg, t->proc_sel_bg, true);
            } else {
                ColorRGB cpu_col = (pe->cpu_pct > 50.0f) ? t->crit : (pe->cpu_pct > 15.0f ? t->warn : t->text);
                grid_print(proc_x + 2, row_y, proc_w - 4, row_line, cpu_col, false, false);
            }
        }
    }

    // ==========================================
    // 3. BOTTOM FOOTER / SHORTCUT BAR
    // ==========================================
    int foot_y = H - 1;
    for (int c = 0; c < W; c++) {
        grid_set(c, foot_y, " ", t->header_fg, t->header_bg, true, false, false);
    }

    char ftr[512];
    if (state->searching) {
        snprintf(ftr, sizeof(ftr), " Search: %s█  (Enter finish, Esc cancel)", state->search_buf);
    } else if (time(NULL) - state->status_msg_time < 3 && state->status_msg[0]) {
        snprintf(ftr, sizeof(ftr), " %s", state->status_msg);
    } else {
        snprintf(ftr, sizeof(ftr), " [q] Exit  [t] Themes  [c] CPU  [m] Mem  [p] PID  [n] Name  [/] Search  [x] Kill  [+/-] Rate: %.1fs",
                 (float)state->refresh_ms / 1000.0f);
    }
    grid_print_bg(1, foot_y, W - 2, ftr, t->header_fg, t->header_bg, true);

    // ==========================================
    // 4. MODALS (THEME PICKER / HELP / KILL)
    // ==========================================
    if (state->modal == MODAL_THEME) {
        int m_w = 66;
        int m_h = 18;
        int m_x = (W - m_w) / 2;
        int m_y = (H - m_h) / 2;
        if (m_x < 1) m_x = 1;
        if (m_y < 1) m_y = 1;

        grid_box(m_x, m_y, m_w, m_h, "THEME CUSTOMIZER", "▲/▼ Browse, Enter Select, Esc Close",
                 t->accent, t->title, t->dim);

        int t_cnt = theme_get_count();
        int visible_themes = m_h - 4;
        for (int i = 0; i < visible_themes && i < t_cnt; i++) {
            const Theme *th = theme_get(i);
            bool is_active = (i == theme_current_index());
            bool is_cur = (i == state->theme_cursor);
            int row_y = m_y + 2 + i;

            if (is_cur) {
                for (int c = m_x + 1; c < m_x + m_w - 1; c++) {
                    grid_set(c, row_y, " ", t->proc_sel_fg, t->proc_sel_bg, true, false, false);
                }
            }

            char item_buf[128];
            snprintf(item_buf, sizeof(item_buf), "%s %-18s %s%s",
                     is_cur ? "▶" : " ",
                     th->name,
                     is_active ? "[ACTIVE] " : "",
                     th->desc);

            if (is_cur) {
                grid_print_bg(m_x + 2, row_y, m_w - 4, item_buf, t->proc_sel_fg, t->proc_sel_bg, true);
            } else {
                grid_print(m_x + 2, row_y, m_w - 4, item_buf, is_active ? t->accent : t->text, is_active, false);
            }
        }
    } else if (state->modal == MODAL_HELP) {
        int m_w = 66;
        int m_h = 19;
        int m_x = (W - m_w) / 2;
        int m_y = (H - m_h) / 2;

        grid_box(m_x, m_y, m_w, m_h, "MACPULSE HELP & SHORTCUTS", "Press Esc or ? to Close",
                 t->accent, t->title, t->dim);

        const char *help_lines[] = {
            "NAVIGATION & CONTROLS:",
            "  q, Ctrl+C       Quit MacPulse",
            "  t               Open Theme Customizer Modal",
            "  T (Shift+T)     Instantly cycle to next theme",
            "  Up / Down, j/k  Scroll Activity Monitor process table",
            "  /               Search and filter processes by name or user",
            "  Escape          Clear search / Close modal",
            "",
            "SORTING PROCESSES:",
            "  c               Sort processes by %CPU usage (descending)",
            "  m               Sort processes by %Memory usage (descending)",
            "  p               Sort processes by PID",
            "  n               Sort processes by Process Name",
            "  r               Reverse current sort order",
            "",
            "MANAGEMENT & SETTINGS:",
            "  x, K            Send SIGTERM / kill highlighted process",
            "  + / -           Speed up / slow down refresh rate (0.2s - 5s)",
            "  h, ?            Toggle this Help dialog"
        };
        for (int i = 0; i < 17; i++) {
            grid_print(m_x + 3, m_y + 1 + i, m_w - 6, help_lines[i],
                       (i == 0 || i == 8 || i == 14) ? t->accent : t->text, (i == 0 || i == 8 || i == 14), false);
        }
    } else if (state->modal == MODAL_KILL_CONFIRM) {
        int m_w = 54;
        int m_h = 7;
        int m_x = (W - m_w) / 2;
        int m_y = (H - m_h) / 2;

        grid_box(m_x, m_y, m_w, m_h, "TERMINATE PROCESS", "Confirm", t->crit, t->title, t->dim);

        char kill_msg[128];
        snprintf(kill_msg, sizeof(kill_msg), "Send SIGTERM to PID %d (%s)?", state->target_kill_pid, state->target_kill_name);
        grid_print(m_x + 4, m_y + 2, m_w - 8, kill_msg, t->text, true, false);
        grid_print(m_x + 4, m_y + 4, m_w - 8, "Press [y] to Kill, or [n / Esc] to Cancel", t->accent, true, false);
    }

    // Flush whole 2D grid in single atomic write call
    grid_flush();
}
