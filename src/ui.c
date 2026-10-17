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


void ui_init(void) {
    buf_init(&s_out_buf, 65536);
    // Enter alternate screen, hide cursor, clear screen, home, disable auto-wrap
    const char *init_seq = "\033[?1049h\033[2J\033[H\033[?25l\033[?7l";
    write(STDOUT_FILENO, init_seq, strlen(init_seq));
}

void ui_cleanup(void) {
    // Re-enable auto-wrap, show cursor, leave alternate screen
    const char *cleanup_seq = "\033[?7h\033[?25h\033[?1049l";
    write(STDOUT_FILENO, cleanup_seq, strlen(cleanup_seq));
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

    // Embed title: ┌─ Title • Extra ───...──┐
    int cur_x = x + 2;
    if (title && title[0]) {
        grid_set(x + 1, y, "─", border_col, (ColorRGB){0,0,0}, false, false, false);
        grid_set(cur_x++, y, " ", border_col, (ColorRGB){0,0,0}, false, false, false);
        int t_len = grid_print(cur_x, y, w - 8, title, title_col, true, false);
        cur_x += t_len;
        grid_set(cur_x++, y, " ", border_col, (ColorRGB){0,0,0}, false, false, false);
    }

    if (extra && extra[0] && (w - cur_x - (int)strlen(extra) - 3) > 0) {
        grid_set(cur_x++, y, "•", border_col, (ColorRGB){0,0,0}, false, false, false);
        grid_set(cur_x++, y, " ", border_col, (ColorRGB){0,0,0}, false, false, false);
        int e_len = grid_print(cur_x, y, w - cur_x - 3, extra, extra_col, false, false);
        cur_x += e_len;
        grid_set(cur_x++, y, " ", border_col, (ColorRGB){0,0,0}, false, false, false);
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

    ColorRGB prev_fg = {0,0,0};
    ColorRGB prev_bg = {0,0,0};
    bool prev_has_bg = false;
    bool prev_bold = false;
    bool prev_dim = false;
    bool in_style = false;

    for (int r = 0; r < s_grid.rows; r++) {
        // Explicit row cursor positioning prevents spurious auto-wrap line feeds
        char cup[32];
        snprintf(cup, sizeof(cup), "\033[%d;1H", r + 1);
        buf_append_str(&s_out_buf, cup);

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
    }
    buf_append_str(&s_out_buf, "\033[0m");

    write(STDOUT_FILENO, s_out_buf.data, s_out_buf.len);
}

// -------------------------------------------------------------
// TTYPLOT GRAPH ENGINE (Authentic ASCII/Unicode Graphing)
// Reference: github.com/tenox7/ttyplot
// -------------------------------------------------------------
static void grid_ttyplot(int x, int y, int w, int h,
                         const char *title, const char *extra,
                         const float *history, int count,
                         float max_val, const char *unit,
                         ColorRGB plot_col, const Theme *t) {
    if (w < 20 || h < 6) return;
    if (max_val <= 0.0f) max_val = 100.0f;

    grid_box(x, y, w, h, title, extra, t->border, t->title, t->dim);

    int y_top = y + 1;
    int y_axis = y + h - 3;
    int y_stat = y + h - 2;
    int plot_h = y_axis - y_top;
    if (plot_h < 2) return;

    int axis_x = x + 2;

    // Top arrow ^ and label
    grid_set(axis_x, y_top, "^", t->accent, (ColorRGB){0,0,0}, false, true, false);

    char top_label[32];
    if (max_val >= 100.0f) {
        snprintf(top_label, sizeof(top_label), "%3.0f %s", max_val, unit);
    } else {
        snprintf(top_label, sizeof(top_label), "%4.1f %s", max_val, unit);
    }
    grid_print(axis_x + 2, y_top, 10, top_label, t->text, false, false);

    // Vertical axis line |
    for (int r = y_top + 1; r < y_axis; r++) {
        grid_set(axis_x, r, "|", t->dim, (ColorRGB){0,0,0}, false, false, false);
    }

    // Horizontal axis line +--------->
    grid_set(axis_x, y_axis, "+", t->accent, (ColorRGB){0,0,0}, false, true, false);
    for (int c = axis_x + 1; c < x + w - 2; c++) {
        grid_set(c, y_axis, "-", t->dim, (ColorRGB){0,0,0}, false, false, false);
    }
    grid_set(x + w - 2, y_axis, ">", t->accent, (ColorRGB){0,0,0}, false, true, false);

    // Ticks & Dotted Grid
    int num_ticks = (plot_h >= 7) ? 3 : (plot_h >= 4 ? 2 : 1);
    float tick_fractions[3] = {0.75f, 0.50f, 0.25f};
    if (num_ticks == 2) {
        tick_fractions[0] = 0.66f;
        tick_fractions[1] = 0.33f;
    } else if (num_ticks == 1) {
        tick_fractions[0] = 0.50f;
    }

    for (int ti = 0; ti < num_ticks; ti++) {
        float f = tick_fractions[ti];
        int r_offset = (int)roundf(f * (float)plot_h);
        int tr = y_axis - r_offset;
        if (tr > y_top && tr < y_axis) {
            char t_lbl[32];
            float tick_val = max_val * f;
            if (max_val >= 100.0f) {
                snprintf(t_lbl, sizeof(t_lbl), "%3.0f %s", tick_val, unit);
            } else {
                snprintf(t_lbl, sizeof(t_lbl), "%4.1f %s", tick_val, unit);
            }
            grid_print(axis_x + 2, tr, 10, t_lbl, t->dim, false, true);

            int data_start = axis_x + 11;
            int data_end = x + w - 3;
            for (int dc = data_start; dc <= data_end; dc++) {
                if ((dc - data_start) % 2 == 0) {
                    grid_set(dc, tr, ".", t->dim, (ColorRGB){0,0,0}, false, false, true);
                }
            }
        }
    }

    // Data Plotting
    const char *blocks[] = {" ", " ", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
    int data_start = axis_x + 11;
    int data_end = x + w - 3;
    int data_w = data_end - data_start + 1;

    if (data_w > 0) {
        for (int c = 0; c < data_w; c++) {
            int hist_idx = count - data_w + c;
            if (hist_idx < 0 || hist_idx >= count) continue;

            float val = history[hist_idx];
            if (val < 0.0f) val = 0.0f;
            float norm = val / max_val;
            if (norm > 1.0f) norm = 1.0f;
            float bar_h = norm * (float)plot_h;

            ColorRGB col = (val > 80.0f) ? t->crit : ((val > 50.0f) ? t->warn : plot_col);
            int screen_c = data_start + c;

            for (int r = 0; r < plot_h; r++) {
                int screen_r = y_axis - 1 - r;
                if (bar_h >= (float)(r + 1)) {
                    grid_set(screen_c, screen_r, "█", col, (ColorRGB){0,0,0}, false, false, false);
                } else if (bar_h > (float)r && bar_h < (float)(r + 1)) {
                    float frac = bar_h - (float)r;
                    int b_idx = (int)(frac * 8.0f);
                    if (b_idx < 1) b_idx = 1;
                    if (b_idx > 8) b_idx = 8;
                    grid_set(screen_c, screen_r, blocks[b_idx], col, (ColorRGB){0,0,0}, false, false, false);
                }
            }
        }
    }

    // Stats row under axis
    grid_set(axis_x, y_stat, "|", t->dim, (ColorRGB){0,0,0}, false, false, false);

    float last_v = (count > 0) ? history[count - 1] : 0.0f;
    float min_v = 999999.0f, max_v_seen = 0.0f, sum_v = 0.0f;
    int n_samples = 0;
    int start_i = (count > data_w) ? count - data_w : 0;
    for (int i = start_i; i < count; i++) {
        float v = history[i];
        if (v < min_v) min_v = v;
        if (v > max_v_seen) max_v_seen = v;
        sum_v += v;
        n_samples++;
    }
    if (n_samples == 0) min_v = 0.0f;
    float avg_v = (n_samples > 0) ? (sum_v / (float)n_samples) : 0.0f;

    char stat_buf[128];
    if (max_val >= 100.0f) {
        snprintf(stat_buf, sizeof(stat_buf), "last=%4.1f min=%4.1f max=%4.1f avg=%4.1f %s",
                 last_v, min_v, max_v_seen, avg_v, unit);
    } else {
        snprintf(stat_buf, sizeof(stat_buf), "last=%.2f min=%.2f max=%.2f avg=%.2f %s",
                 last_v, min_v, max_v_seen, avg_v, unit);
    }
    grid_print(axis_x + 2, y_stat, w - 8, stat_buf, t->text, true, false);

    if (w >= 48) {
        const char *tag = "ttyplot 1.0";
        grid_print(x + w - (int)strlen(tag) - 3, y_stat, 16, tag, t->dim, false, true);
    }
}

// Dual series ttyplot (overlay series 1 as solid bars, series 2 as dots)
static void grid_ttyplot_dual(int x, int y, int w, int h,
                              const char *title, const char *extra,
                              const float *hist1, int count1, const char *name1, ColorRGB col1,
                              const float *hist2, int count2, const char *name2, ColorRGB col2,
                              float max_val, const char *unit,
                              const Theme *t) {
    if (w < 20 || h < 6) return;

    if (max_val <= 0.0f) {
        float m = 1.0f;
        for (int i = 0; i < count1; i++) if (hist1[i] > m) m = hist1[i];
        for (int i = 0; i < count2; i++) if (hist2[i] > m) m = hist2[i];
        max_val = m * 1.15f;
    }

    grid_box(x, y, w, h, title, extra, t->border, t->title, t->dim);

    int y_top = y + 1;
    int y_axis = y + h - 3;
    int y_stat = y + h - 2;
    int plot_h = y_axis - y_top;
    if (plot_h < 2) return;

    int axis_x = x + 2;

    // Top arrow ^ and label
    grid_set(axis_x, y_top, "^", t->accent, (ColorRGB){0,0,0}, false, true, false);

    char top_label[32];
    if (max_val >= 100.0f) {
        snprintf(top_label, sizeof(top_label), "%3.0f %s", max_val, unit);
    } else {
        snprintf(top_label, sizeof(top_label), "%4.1f %s", max_val, unit);
    }
    grid_print(axis_x + 2, y_top, 10, top_label, t->text, false, false);

    // Vertical line |
    for (int r = y_top + 1; r < y_axis; r++) {
        grid_set(axis_x, r, "|", t->dim, (ColorRGB){0,0,0}, false, false, false);
    }

    // Horizontal axis line +--------->
    grid_set(axis_x, y_axis, "+", t->accent, (ColorRGB){0,0,0}, false, true, false);
    for (int c = axis_x + 1; c < x + w - 2; c++) {
        grid_set(c, y_axis, "-", t->dim, (ColorRGB){0,0,0}, false, false, false);
    }
    grid_set(x + w - 2, y_axis, ">", t->accent, (ColorRGB){0,0,0}, false, true, false);

    // Ticks & Dotted Grid
    int num_ticks = (plot_h >= 7) ? 3 : (plot_h >= 4 ? 2 : 1);
    float tick_fractions[3] = {0.75f, 0.50f, 0.25f};
    if (num_ticks == 2) {
        tick_fractions[0] = 0.66f;
        tick_fractions[1] = 0.33f;
    } else if (num_ticks == 1) {
        tick_fractions[0] = 0.50f;
    }

    for (int ti = 0; ti < num_ticks; ti++) {
        float f = tick_fractions[ti];
        int r_offset = (int)roundf(f * (float)plot_h);
        int tr = y_axis - r_offset;
        if (tr > y_top && tr < y_axis) {
            char t_lbl[32];
            float tick_val = max_val * f;
            if (max_val >= 100.0f) {
                snprintf(t_lbl, sizeof(t_lbl), "%3.0f %s", tick_val, unit);
            } else {
                snprintf(t_lbl, sizeof(t_lbl), "%4.1f %s", tick_val, unit);
            }
            grid_print(axis_x + 2, tr, 10, t_lbl, t->dim, false, true);

            int data_start = axis_x + 11;
            int data_end = x + w - 3;
            for (int dc = data_start; dc <= data_end; dc++) {
                if ((dc - data_start) % 2 == 0) {
                    grid_set(dc, tr, ".", t->dim, (ColorRGB){0,0,0}, false, false, true);
                }
            }
        }
    }

    // Data Plotting: Series 1 as bars, Series 2 as dots
    const char *blocks[] = {" ", " ", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
    int data_start = axis_x + 11;
    int data_end = x + w - 3;
    int data_w = data_end - data_start + 1;

    if (data_w > 0) {
        for (int c = 0; c < data_w; c++) {
            int screen_c = data_start + c;

            // Series 1 (Bars)
            int idx1 = count1 - data_w + c;
            float val1 = (idx1 >= 0 && idx1 < count1) ? hist1[idx1] : 0.0f;
            if (val1 < 0.0f) val1 = 0.0f;
            float norm1 = val1 / max_val;
            if (norm1 > 1.0f) norm1 = 1.0f;
            float bar_h1 = norm1 * (float)plot_h;

            for (int r = 0; r < plot_h; r++) {
                int screen_r = y_axis - 1 - r;
                if (bar_h1 >= (float)(r + 1)) {
                    grid_set(screen_c, screen_r, "█", col1, (ColorRGB){0,0,0}, false, false, false);
                } else if (bar_h1 > (float)r && bar_h1 < (float)(r + 1)) {
                    float frac = bar_h1 - (float)r;
                    int b_idx = (int)(frac * 8.0f);
                    if (b_idx < 1) b_idx = 1;
                    if (b_idx > 8) b_idx = 8;
                    grid_set(screen_c, screen_r, blocks[b_idx], col1, (ColorRGB){0,0,0}, false, false, false);
                }
            }

            // Series 2 (Dots/Points)
            int idx2 = count2 - data_w + c;
            if (idx2 >= 0 && idx2 < count2) {
                float val2 = hist2[idx2];
                if (val2 < 0.0f) val2 = 0.0f;
                float norm2 = val2 / max_val;
                if (norm2 > 1.0f) norm2 = 1.0f;
                int dot_r = (int)roundf(norm2 * (float)(plot_h - 1));
                int screen_r2 = y_axis - 1 - dot_r;
                grid_set(screen_c, screen_r2, "•", col2, (ColorRGB){0,0,0}, false, true, false);
            }
        }
    }

    // Stats row under axis
    grid_set(axis_x, y_stat, "|", t->dim, (ColorRGB){0,0,0}, false, false, false);

    float last1 = (count1 > 0) ? hist1[count1 - 1] : 0.0f;
    float sum1 = 0.0f; int n1 = 0;
    for (int i = (count1 > data_w ? count1 - data_w : 0); i < count1; i++) { sum1 += hist1[i]; n1++; }
    float avg1 = (n1 > 0) ? (sum1 / n1) : 0.0f;

    float last2 = (count2 > 0) ? hist2[count2 - 1] : 0.0f;
    float sum2 = 0.0f; int n2 = 0;
    for (int i = (count2 > data_w ? count2 - data_w : 0); i < count2; i++) { sum2 += hist2[i]; n2++; }
    float avg2 = (n2 > 0) ? (sum2 / n2) : 0.0f;

    char stat_buf[256];
    snprintf(stat_buf, sizeof(stat_buf), "█ %s: last=%.1f avg=%.1f%s  • %s: last=%.1f avg=%.1f%s",
             name1, last1, avg1, unit, name2, last2, avg2, unit);
    grid_print(axis_x + 2, y_stat, w - 8, stat_buf, t->text, true, false);

    if (w >= 50) {
        const char *tag = "ttyplot 1.0";
        grid_print(x + w - (int)strlen(tag) - 3, y_stat, 16, tag, t->dim, false, true);
    }
}

// -------------------------------------------------------------
// 4 MAIN FEATURE READING CARDS
// -------------------------------------------------------------
static void draw_cpu_card(int x, int y, int w, int h, const SystemTelemetry *telem, const Theme *t) {
    char extra[64] = "";
    if (telem->cpu_temp_avg > 0) {
        snprintf(extra, sizeof(extra), "%.1f°C", telem->cpu_temp_avg);
    }
    grid_box(x, y, w, h, "CPU MONITOR", extra, t->border, t->cpu_color, t->title);

    int cur_y = y + 1;
    grid_bar(x + 2, cur_y, w - 10, telem->cpu_total_pct, t->cpu_color, t->dim, t->text);
    cur_y++;

    if (cur_y < y + h - 1) {
        char line1[128];
        snprintf(line1, sizeof(line1), "User: %2.0f%%  Sys: %2.0f%%",
                 telem->cpu_user_pct, telem->cpu_sys_pct);
        grid_print(x + 2, cur_y, w - 4, line1, t->dim, false, false);
        cur_y++;
    }

    if (cur_y < y + h - 1) {
        char line2[128];
        snprintf(line2, sizeof(line2), "Load: %.2f %.2f %.2f",
                 telem->load_avg[0], telem->load_avg[1], telem->load_avg[2]);
        grid_print(x + 2, cur_y, w - 4, line2, t->dim, false, false);
        cur_y++;
    }

    if (cur_y < y + h - 1) {
        char line3[128];
        snprintf(line3, sizeof(line3), "%d Cores (%dE + %dP)",
                 telem->total_cores, telem->e_cores, telem->p_cores);
        grid_print(x + 2, cur_y, w - 4, line3, t->dim, false, false);
    }
}

static void draw_gpu_card(int x, int y, int w, int h, const SystemTelemetry *telem, const Theme *t) {
    char extra[64] = "";
    if (telem->gpu_temp_avg > 0) {
        snprintf(extra, sizeof(extra), "%.1f°C", telem->gpu_temp_avg);
    }
    grid_box(x, y, w, h, "GPU & VRAM", extra, t->border, t->gpu_color, t->title);

    int cur_y = y + 1;
    grid_bar(x + 2, cur_y, w - 10, telem->gpu_device_pct, t->gpu_color, t->dim, t->text);
    cur_y++;

    if (cur_y < y + h - 1) {
        char line1[128];
        snprintf(line1, sizeof(line1), "Renderer: %2.0f%%  Tiler: %2.0f%%",
                 telem->gpu_renderer_pct, telem->gpu_tiler_pct);
        grid_print(x + 2, cur_y, w - 4, line1, t->dim, false, false);
        cur_y++;
    }

    if (cur_y < y + h - 1) {
        char in_vram[32], alloc_vram[32];
        format_bytes(telem->gpu_in_use_bytes, in_vram, sizeof(in_vram));
        format_bytes(telem->gpu_alloc_bytes, alloc_vram, sizeof(alloc_vram));
        char line2[128];
        snprintf(line2, sizeof(line2), "VRAM: %s / %s", in_vram, alloc_vram);
        grid_print(x + 2, cur_y, w - 4, line2, t->dim, false, false);
        cur_y++;
    }

    if (cur_y < y + h - 1) {
        char line3[128];
        snprintf(line3, sizeof(line3), "%d GPU Cores • Metal 3", telem->gpu_cores);
        grid_print(x + 2, cur_y, w - 4, line3, t->dim, false, false);
    }
}

static void draw_mem_card(int x, int y, int w, int h, const SystemTelemetry *telem, const Theme *t) {
    char used_str[32], tot_str[32];
    format_bytes(telem->mem_used_bytes, used_str, sizeof(used_str));
    format_bytes(telem->mem_total_bytes, tot_str, sizeof(tot_str));
    char extra[64];
    snprintf(extra, sizeof(extra), "%s", used_str);
    grid_box(x, y, w, h, "UNIFIED MEMORY", extra, t->border, t->mem_color, t->title);

    int cur_y = y + 1;
    grid_bar(x + 2, cur_y, w - 10, telem->mem_used_pct, t->mem_color, t->dim, t->text);
    cur_y++;

    if (cur_y < y + h - 1) {
        char app_str[32], wired_str[32];
        format_bytes(telem->mem_active_bytes, app_str, sizeof(app_str));
        format_bytes(telem->mem_wired_bytes, wired_str, sizeof(wired_str));
        char line1[128];
        snprintf(line1, sizeof(line1), "App: %s  Wired: %s", app_str, wired_str);
        grid_print(x + 2, cur_y, w - 4, line1, t->dim, false, false);
        cur_y++;
    }

    if (cur_y < y + h - 1) {
        char comp_str[32], cache_str[32];
        format_bytes(telem->mem_compressed_bytes, comp_str, sizeof(comp_str));
        format_bytes(telem->mem_inactive_bytes, cache_str, sizeof(cache_str));
        char line2[128];
        snprintf(line2, sizeof(line2), "Comp: %s  Cache: %s", comp_str, cache_str);
        grid_print(x + 2, cur_y, w - 4, line2, t->dim, false, false);
        cur_y++;
    }

    if (cur_y < y + h - 1) {
        char free_str[32], swap_str[32];
        format_bytes(telem->mem_free_bytes, free_str, sizeof(free_str));
        format_bytes(telem->swap_used_bytes, swap_str, sizeof(swap_str));
        char line3[128];
        snprintf(line3, sizeof(line3), "Free: %s  Swap: %s", free_str, swap_str);
        grid_print(x + 2, cur_y, w - 4, line3, t->dim, false, false);
    }
}

static void draw_net_card(int x, int y, int w, int h, const SystemTelemetry *telem, const Theme *t) {
    char down_s[32], up_s[32];
    format_speed(telem->net_download_bps, down_s, sizeof(down_s));
    format_speed(telem->net_upload_bps, up_s, sizeof(up_s));
    grid_box(x, y, w, h, "NETWORK SPEED", NULL, t->border, t->accent, t->title);

    int cur_y = y + 1;
    char dl_line[128];
    snprintf(dl_line, sizeof(dl_line), "Download: ▼ %s", down_s);
    grid_print(x + 2, cur_y, w - 4, dl_line, t->accent, true, false);
    cur_y++;

    if (cur_y < y + h - 1) {
        char ul_line[128];
        snprintf(ul_line, sizeof(ul_line), "Upload:   ▲ %s", up_s);
        grid_print(x + 2, cur_y, w - 4, ul_line, t->accent, true, false);
        cur_y++;
    }

    if (cur_y < y + h - 1) {
        char in_tot[32], out_tot[32];
        format_bytes(telem->net_total_in_bytes, in_tot, sizeof(in_tot));
        format_bytes(telem->net_total_out_bytes, out_tot, sizeof(out_tot));
        char line2[128];
        snprintf(line2, sizeof(line2), "In: %s  Out: %s", in_tot, out_tot);
        grid_print(x + 2, cur_y, w - 4, line2, t->dim, false, false);
        cur_y++;
    }

    if (cur_y < y + h - 1) {
        char r_s[32], w_s[32];
        format_speed(telem->disk_read_bps, r_s, sizeof(r_s));
        format_speed(telem->disk_write_bps, w_s, sizeof(w_s));
        char line3[128];
        snprintf(line3, sizeof(line3), "Disk IO: R %s W %s", r_s, w_s);
        grid_print(x + 2, cur_y, w - 4, line3, t->dim, false, false);
    }
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
    snprintf(hdr_left, sizeof(hdr_left), "◈ MacPulse • %s • %d Cores (%dE+%dP) • %d GPU • v1.0.0",
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

    for (int c = 0; c < W; c++) {
        grid_set(c, 0, " ", t->header_fg, t->header_bg, true, false, false);
    }
    grid_print_bg(1, 0, W - 2, hdr_left, t->header_fg, t->header_bg, true);
    int right_x = W - (int)strlen(hdr_right) - 1;
    if (right_x > (int)strlen(hdr_left) + 3) {
        grid_print_bg(right_x, 0, W - right_x - 1, hdr_right, t->header_fg, t->header_bg, false);
    }

    // ==========================================
    // 2. 4 MAIN FEATURE CARDS
    //    [CPU MONITOR] [GPU & VRAM] [UNIFIED MEMORY] [NETWORK SPEED]
    // ==========================================
    int card_h = 6;
    int card_bottom_y = 1;

    if (W >= 108) {
        // 4 Columns side-by-side
        int col_w = W / 4;
        draw_cpu_card(0, 1, col_w, card_h, telem, t);
        draw_gpu_card(col_w, 1, col_w, card_h, telem, t);
        draw_mem_card(col_w * 2, 1, col_w, card_h, telem, t);
        draw_net_card(col_w * 3, 1, W - col_w * 3, card_h, telem, t);
        card_bottom_y = 1 + card_h;
    } else {
        // 2x2 Grid of Cards
        int half_w = W / 2;
        int rem_w = W - half_w;
        draw_cpu_card(0, 1, half_w, card_h, telem, t);
        draw_gpu_card(half_w, 1, rem_w, card_h, telem, t);
        draw_mem_card(0, 1 + card_h, half_w, card_h, telem, t);
        draw_net_card(half_w, 1 + card_h, rem_w, card_h, telem, t);
        card_bottom_y = 1 + card_h * 2;
    }

    // ==========================================
    // 3. GRAPHICAL HISTORY SECTION (TTYPLOT REFERENCE)
    // ==========================================
    int total_avail = (H - 1) - card_bottom_y;
    if (total_avail < 6) total_avail = 6;

    bool show_process_table = state->show_procs && (state->view_mode != VIEW_ZEN) && (total_avail >= 16);

    int graph_y = card_bottom_y;
    int graph_h = show_process_table ? (total_avail * 58 / 100) : total_avail;
    if (graph_h < 7) graph_h = 7;
    if (show_process_table && (total_avail - graph_h < 6)) {
        graph_h = total_avail - 6;
    }

    switch (state->graph_mode) {
        case GRAPH_MODE_SPLIT: {
            if (W >= 90) {
                int left_w = W / 2;
                int right_w = W - left_w;
                grid_ttyplot(0, graph_y, left_w, graph_h,
                             "CPU USAGE HISTORY", "Apple Silicon Cores",
                             telem->history_cpu, telem->history_cpu_count,
                             100.0f, "%", t->cpu_color, t);
                grid_ttyplot(left_w, graph_y, right_w, graph_h,
                             "GPU USAGE HISTORY", "IOAccelerator",
                             telem->history_gpu, telem->history_gpu_count,
                             100.0f, "%", t->gpu_color, t);
            } else {
                int gh1 = graph_h / 2;
                int gh2 = graph_h - gh1;
                grid_ttyplot(0, graph_y, W, gh1,
                             "CPU USAGE HISTORY", NULL,
                             telem->history_cpu, telem->history_cpu_count,
                             100.0f, "%", t->cpu_color, t);
                grid_ttyplot(0, graph_y + gh1, W, gh2,
                             "GPU USAGE HISTORY", NULL,
                             telem->history_gpu, telem->history_gpu_count,
                             100.0f, "%", t->gpu_color, t);
            }
            break;
        }
        case GRAPH_MODE_STACKED: {
            int gh1 = graph_h / 2;
            int gh2 = graph_h - gh1;
            grid_ttyplot(0, graph_y, W, gh1,
                         "CPU USAGE HISTORY", "Apple Silicon Cores",
                         telem->history_cpu, telem->history_cpu_count,
                         100.0f, "%", t->cpu_color, t);
            grid_ttyplot(0, graph_y + gh1, W, gh2,
                         "GPU USAGE HISTORY", "IOAccelerator",
                         telem->history_gpu, telem->history_gpu_count,
                         100.0f, "%", t->gpu_color, t);
            break;
        }
        case GRAPH_MODE_COMBINED: {
            grid_ttyplot_dual(0, graph_y, W, graph_h,
                              "CPU & GPU USAGE DUAL-SERIES HISTORY", "█ CPU • GPU",
                              telem->history_cpu, telem->history_cpu_count, "CPU", t->cpu_color,
                              telem->history_gpu, telem->history_gpu_count, "GPU", t->gpu_color,
                              100.0f, "%", t);
            break;
        }
        case GRAPH_MODE_NET: {
            grid_ttyplot_dual(0, graph_y, W, graph_h,
                              "NETWORK BANDWIDTH THROUGHPUT HISTORY", "▼ Down • Up",
                              telem->history_net_down, telem->history_net_count, "Down", t->accent,
                              telem->history_net_up, telem->history_net_count, "Up", t->warn,
                              0.0f, "KB/s", t);
            break;
        }
    }

    // ==========================================
    // 4. BOTTOM ACTIVITY MONITOR (PROCESS TABLE)
    // ==========================================
    if (show_process_table) {
        int proc_y = graph_y + graph_h;
        int proc_h = (H - 1) - proc_y;
        if (proc_h >= 4) {
            char proc_title[64];
            if (state->search_buf[0]) {
                snprintf(proc_title, sizeof(proc_title), "ACTIVITY MONITOR (%d/%d, /:\"%s\")",
                         telem->procs.filtered_count, telem->procs.count, state->search_buf);
            } else {
                snprintf(proc_title, sizeof(proc_title), "ACTIVITY MONITOR (%d processes)", telem->procs.count);
            }
            grid_box(0, proc_y, W, proc_h, proc_title, "↑/↓ scroll, / search, x kill, Tab toggle",
                     t->border, t->title, t->dim);

            int tbl_y = proc_y + 1;
            char col_hdr[256];
            snprintf(col_hdr, sizeof(col_hdr), "%-6s  %-12s  %-9s  %5s  %5s  %5s  %-7s  %s",
                     "PID", "USER", "RES", "%CPU", "%GPU", "%MEM", "TIME", "COMMAND");
            grid_print(2, tbl_y, W - 4, col_hdr, t->accent, true, false);

            int avail_rows = proc_h - 3;
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
                    snprintf(row_line, sizeof(row_line), "%-6d  %-12.12s  %-9s  %4.1f%%  %4.1f%%  %4.1f%%  %-7s  %s",
                             pe->pid, pe->username, res_str, pe->cpu_pct, pe->gpu_pct, pe->mem_pct, time_s, pe->name);

                    bool is_sel = (idx == state->proc_selected);
                    if (is_sel) {
                        for (int c = 1; c < W - 1; c++) {
                            grid_set(c, row_y, " ", t->proc_sel_fg, t->proc_sel_bg, true, false, false);
                        }
                        grid_print_bg(2, row_y, W - 4, row_line, t->proc_sel_fg, t->proc_sel_bg, true);
                    } else {
                        ColorRGB cpu_col = (pe->cpu_pct > 50.0f) ? t->crit : (pe->cpu_pct > 15.0f ? t->warn : t->text);
                        grid_print(2, row_y, W - 4, row_line, cpu_col, false, false);
                    }
                }
            }
        }
    }

    // ==========================================
    // 5. BOTTOM FOOTER / SHORTCUT BAR
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
        const char *g_mode_str = (state->graph_mode == GRAPH_MODE_SPLIT) ? "Split" :
                                 ((state->graph_mode == GRAPH_MODE_STACKED) ? "Stack" :
                                 ((state->graph_mode == GRAPH_MODE_COMBINED) ? "Dual" : "Net"));
        snprintf(ftr, sizeof(ftr), " [q]Exit [t]Themes [g]Graphs:%s [Tab]%s [c]CPU [m]Mem [p]PID [/]Search [x]Kill [+/-]%.1fs",
                 g_mode_str,
                 show_process_table ? "HideProcs" : "ShowProcs",
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
        int m_w = 68;
        int m_h = 22;
        int m_x = (W - m_w) / 2;
        int m_y = (H - m_h) / 2;

        grid_box(m_x, m_y, m_w, m_h, "MACPULSE HELP & SHORTCUTS", "Press Esc or ? to Close",
                 t->accent, t->title, t->dim);

        const char *help_lines[] = {
            "NAVIGATION & CONTROLS:",
            "  q, Ctrl+C       Quit MacPulse",
            "  t               Open Theme Customizer Modal",
            "  T (Shift+T)     Instantly cycle to next theme",
            "  g               Cycle Graph Modes (Split / Stack / Dual / Net)",
            "  Tab             Toggle Process Table / Maximize Graphs",
            "  Up / Down, j/k  Scroll Activity Monitor process table",
            "  /               Search and filter processes by name or user",
            "  Escape          Clear search / Close active modal",
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
        for (int i = 0; i < 20; i++) {
            grid_print(m_x + 3, m_y + 1 + i, m_w - 6, help_lines[i],
                       (i == 0 || i == 9 || i == 16) ? t->accent : t->text, (i == 0 || i == 9 || i == 16), false);
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
