#include "ui.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define MAX_GRID_COLS 320
#define MAX_GRID_ROWS 160

typedef struct {
  char ch[8]; // Multi-byte UTF-8 or single ASCII
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
  if (b->data)
    b->data[0] = '\0';
}

static void buf_reset(StrBuf *b) {
  b->len = 0;
  if (b->data)
    b->data[0] = '\0';
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
    size_t new_cap = (b->cap * 2 > b->len + add_len + 4096)
                         ? b->cap * 2
                         : (b->len + add_len + 4096);
    b->data = realloc(b->data, new_cap);
    b->cap = new_cap;
  }
}

static void buf_append_str(StrBuf *b, const char *s) {
  if (!s)
    return;
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
  if (cols > MAX_GRID_COLS)
    cols = MAX_GRID_COLS;
  if (rows > MAX_GRID_ROWS)
    rows = MAX_GRID_ROWS;
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

static void grid_set(int c, int r, const char *utf8_char, ColorRGB fg,
                     ColorRGB bg, bool has_bg, bool bold, bool dim) {
  if (c < 0 || c >= s_grid.cols || r < 0 || r >= s_grid.rows)
    return;
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

static int grid_print(int c, int r, int max_w, const char *text, ColorRGB fg,
                      bool bold, bool dim) {
  if (r < 0 || r >= s_grid.rows || c >= s_grid.cols || !text)
    return 0;
  int written = 0;
  const unsigned char *p = (const unsigned char *)text;

  while (*p && (max_w <= 0 || written < max_w) && (c + written) < s_grid.cols) {
    char utf8_ch[8] = {0};
    int ch_len = 1;

    if (*p < 0x80) {
      utf8_ch[0] = *p;
      ch_len = 1;
    } else if ((*p & 0xE0) == 0xC0) {
      utf8_ch[0] = p[0];
      utf8_ch[1] = p[1];
      ch_len = 2;
    } else if ((*p & 0xF0) == 0xE0) {
      utf8_ch[0] = p[0];
      utf8_ch[1] = p[1];
      utf8_ch[2] = p[2];
      ch_len = 3;
    } else if ((*p & 0xF8) == 0xF0) {
      utf8_ch[0] = p[0];
      utf8_ch[1] = p[1];
      utf8_ch[2] = p[2];
      utf8_ch[3] = p[3];
      ch_len = 4;
    }

    grid_set(c + written, r, utf8_ch, fg, (ColorRGB){0, 0, 0}, false, bold,
             dim);
    written++;
    p += ch_len;
  }
  return written;
}

static void grid_print_bg(int c, int r, int max_w, const char *text,
                          ColorRGB fg, ColorRGB bg, bool bold) {
  if (r < 0 || r >= s_grid.rows || c >= s_grid.cols || !text)
    return;
  int written = 0;
  const unsigned char *p = (const unsigned char *)text;

  while (*p && (max_w <= 0 || written < max_w) && (c + written) < s_grid.cols) {
    char utf8_ch[8] = {0};
    int ch_len = 1;

    if (*p < 0x80) {
      utf8_ch[0] = *p;
      ch_len = 1;
    } else if ((*p & 0xE0) == 0xC0) {
      utf8_ch[0] = p[0];
      utf8_ch[1] = p[1];
      ch_len = 2;
    } else if ((*p & 0xF0) == 0xE0) {
      utf8_ch[0] = p[0];
      utf8_ch[1] = p[1];
      utf8_ch[2] = p[2];
      ch_len = 3;
    } else if ((*p & 0xF8) == 0xF0) {
      utf8_ch[0] = p[0];
      utf8_ch[1] = p[1];
      utf8_ch[2] = p[2];
      utf8_ch[3] = p[3];
      ch_len = 4;
    }

    grid_set(c + written, r, utf8_ch, fg, bg, true, bold, false);
    written++;
    p += ch_len;
  }
}

// Draw framed box with clean hacker border
static void grid_box(int x, int y, int w, int h, const char *title,
                     const char *extra, ColorRGB border_col, ColorRGB title_col,
                     ColorRGB extra_col) {
  if (w < 4 || h < 2)
    return;

  // Corners & horizontal edges
  grid_set(x, y, "┌", border_col, (ColorRGB){0, 0, 0}, false, false, false);
  grid_set(x + w - 1, y, "┐", border_col, (ColorRGB){0, 0, 0}, false, false,
           false);
  grid_set(x, y + h - 1, "└", border_col, (ColorRGB){0, 0, 0}, false, false,
           false);
  grid_set(x + w - 1, y + h - 1, "┘", border_col, (ColorRGB){0, 0, 0}, false,
           false, false);

  for (int c = x + 1; c < x + w - 1; c++) {
    grid_set(c, y, "─", border_col, (ColorRGB){0, 0, 0}, false, false, false);
    grid_set(c, y + h - 1, "─", border_col, (ColorRGB){0, 0, 0}, false, false,
             false);
  }

  // Vertical edges & clear inside
  for (int r = y + 1; r < y + h - 1; r++) {
    grid_set(x, r, "│", border_col, (ColorRGB){0, 0, 0}, false, false, false);
    grid_set(x + w - 1, r, "│", border_col, (ColorRGB){0, 0, 0}, false, false,
             false);
    for (int c = x + 1; c < x + w - 1; c++) {
      grid_set(c, r, " ", border_col, (ColorRGB){0, 0, 0}, false, false, false);
    }
  }

  // Embed title: ┌─ Title • Extra ───...──┐
  int cur_x = x + 2;
  if (title && title[0]) {
    grid_set(x + 1, y, "─", border_col, (ColorRGB){0, 0, 0}, false, false,
             false);
    grid_set(cur_x++, y, " ", border_col, (ColorRGB){0, 0, 0}, false, false,
             false);
    int t_len = grid_print(cur_x, y, w - 8, title, title_col, true, false);
    cur_x += t_len;
    grid_set(cur_x++, y, " ", border_col, (ColorRGB){0, 0, 0}, false, false,
             false);
  }

  if (extra && extra[0] && (w - cur_x - (int)strlen(extra) - 3) > 0) {
    grid_set(cur_x++, y, "•", border_col, (ColorRGB){0, 0, 0}, false, false,
             false);
    grid_set(cur_x++, y, " ", border_col, (ColorRGB){0, 0, 0}, false, false,
             false);
    int e_len =
        grid_print(cur_x, y, w - cur_x - 3, extra, extra_col, false, false);
    cur_x += e_len;
    grid_set(cur_x++, y, " ", border_col, (ColorRGB){0, 0, 0}, false, false,
             false);
  }
}

// Classic Linux / Hacker terminal bar gauge:
// [||||||||||||||||....................] 46%
static void grid_bar(int x, int y, int width, float pct, ColorRGB fill_col,
                     ColorRGB empty_col, ColorRGB text_col) {
  if (width < 6)
    return;
  if (pct < 0.0f)
    pct = 0.0f;
  if (pct > 100.0f)
    pct = 100.0f;

  grid_set(x, y, "[", fill_col, (ColorRGB){0, 0, 0}, false, true, false);
  grid_set(x + width - 1, y, "]", fill_col, (ColorRGB){0, 0, 0}, false, true,
           false);

  int inner_w = width - 2;
  int filled = (int)((pct / 100.0f) * (float)inner_w);
  if (filled > inner_w)
    filled = inner_w;

  for (int i = 0; i < filled; i++) {
    grid_set(x + 1 + i, y, "|", fill_col, (ColorRGB){0, 0, 0}, false, true,
             false);
  }
  for (int i = filled; i < inner_w; i++) {
    grid_set(x + 1 + i, y, ".", empty_col, (ColorRGB){0, 0, 0}, false, false,
             true);
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

  ColorRGB prev_fg = {0, 0, 0};
  ColorRGB prev_bg = {0, 0, 0};
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

      bool fg_change =
          !in_style || memcmp(&cell->fg, &prev_fg, sizeof(ColorRGB)) != 0;
      bool bg_change =
          !in_style || (cell->has_bg != prev_has_bg) ||
          (cell->has_bg && memcmp(&cell->bg, &prev_bg, sizeof(ColorRGB)) != 0);
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
        if (cell->bold)
          buf_append_str(&s_out_buf, "\033[1m");
        if (cell->dim)
          buf_append_str(&s_out_buf, "\033[2m");

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
static void grid_ttyplot(int x, int y, int w, int h, const char *title,
                         const char *extra, const float *history, int count,
                         float max_val, const char *unit, ColorRGB plot_col,
                         const Theme *t) {
  if (w < 20 || h < 6)
    return;
  if (max_val <= 0.0f)
    max_val = 100.0f;

  grid_box(x, y, w, h, title, extra, t->border, t->title, t->dim);

  int y_top = y + 1;
  int y_axis = y + h - 3;
  int y_stat = y + h - 2;
  int plot_h = y_axis - y_top;
  if (plot_h < 2)
    return;

  int axis_x = x + 2;

  // Top arrow ^ and label
  grid_set(axis_x, y_top, "^", t->accent, (ColorRGB){0, 0, 0}, false, true,
           false);

  char top_label[32];
  if (max_val >= 100.0f) {
    snprintf(top_label, sizeof(top_label), "%3.0f %s", max_val, unit);
  } else {
    snprintf(top_label, sizeof(top_label), "%4.1f %s", max_val, unit);
  }
  grid_print(axis_x + 2, y_top, 10, top_label, t->text, false, false);

  // Vertical axis line |
  for (int r = y_top + 1; r < y_axis; r++) {
    grid_set(axis_x, r, "|", t->dim, (ColorRGB){0, 0, 0}, false, false, false);
  }

  // Horizontal axis line +--------->
  grid_set(axis_x, y_axis, "+", t->accent, (ColorRGB){0, 0, 0}, false, true,
           false);
  for (int c = axis_x + 1; c < x + w - 2; c++) {
    grid_set(c, y_axis, "-", t->dim, (ColorRGB){0, 0, 0}, false, false, false);
  }
  grid_set(x + w - 2, y_axis, ">", t->accent, (ColorRGB){0, 0, 0}, false, true,
           false);

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
          grid_set(dc, tr, ".", t->dim, (ColorRGB){0, 0, 0}, false, false,
                   true);
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
      if (hist_idx < 0 || hist_idx >= count)
        continue;

      float val = history[hist_idx];
      if (val < 0.0f)
        val = 0.0f;
      float norm = val / max_val;
      if (norm > 1.0f)
        norm = 1.0f;
      float bar_h = norm * (float)plot_h;

      ColorRGB col =
          (val > 80.0f) ? t->crit : ((val > 50.0f) ? t->warn : plot_col);
      int screen_c = data_start + c;

      for (int r = 0; r < plot_h; r++) {
        int screen_r = y_axis - 1 - r;
        if (bar_h >= (float)(r + 1)) {
          grid_set(screen_c, screen_r, "█", col, (ColorRGB){0, 0, 0}, false,
                   false, false);
        } else if (bar_h > (float)r && bar_h < (float)(r + 1)) {
          float frac = bar_h - (float)r;
          int b_idx = (int)(frac * 8.0f);
          if (b_idx < 1)
            b_idx = 1;
          if (b_idx > 8)
            b_idx = 8;
          grid_set(screen_c, screen_r, blocks[b_idx], col, (ColorRGB){0, 0, 0},
                   false, false, false);
        }
      }
    }
  }

  // Stats row under axis
  grid_set(axis_x, y_stat, "|", t->dim, (ColorRGB){0, 0, 0}, false, false,
           false);

  float last_v = (count > 0) ? history[count - 1] : 0.0f;
  float min_v = 999999.0f, max_v_seen = 0.0f, sum_v = 0.0f;
  int n_samples = 0;
  int start_i = (count > data_w) ? count - data_w : 0;
  for (int i = start_i; i < count; i++) {
    float v = history[i];
    if (v < min_v)
      min_v = v;
    if (v > max_v_seen)
      max_v_seen = v;
    sum_v += v;
    n_samples++;
  }
  if (n_samples == 0)
    min_v = 0.0f;
  float avg_v = (n_samples > 0) ? (sum_v / (float)n_samples) : 0.0f;

  char stat_buf[128];
  if (max_val >= 100.0f) {
    snprintf(stat_buf, sizeof(stat_buf),
             "last=%4.1f min=%4.1f max=%4.1f avg=%4.1f %s", last_v, min_v,
             max_v_seen, avg_v, unit);
  } else {
    snprintf(stat_buf, sizeof(stat_buf),
             "last=%.2f min=%.2f max=%.2f avg=%.2f %s", last_v, min_v,
             max_v_seen, avg_v, unit);
  }
  grid_print(axis_x + 2, y_stat, w - 8, stat_buf, t->text, true, false);

  if (w >= 48) {
    const char *tag = "ttyplot 1.0";
    grid_print(x + w - (int)strlen(tag) - 3, y_stat, 16, tag, t->dim, false,
               true);
  }
}

// Dual series ttyplot (overlay series 1 as solid bars, series 2 as dots)
static void grid_ttyplot_dual(int x, int y, int w, int h, const char *title,
                              const char *extra, const float *hist1, int count1,
                              const char *name1, ColorRGB col1,
                              const float *hist2, int count2, const char *name2,
                              ColorRGB col2, float max_val, const char *unit,
                              const Theme *t) {
  if (w < 20 || h < 6)
    return;

  if (max_val <= 0.0f) {
    float m = 1.0f;
    for (int i = 0; i < count1; i++)
      if (hist1[i] > m)
        m = hist1[i];
    for (int i = 0; i < count2; i++)
      if (hist2[i] > m)
        m = hist2[i];
    max_val = m * 1.15f;
  }

  grid_box(x, y, w, h, title, extra, t->border, t->title, t->dim);

  int y_top = y + 1;
  int y_axis = y + h - 3;
  int y_stat = y + h - 2;
  int plot_h = y_axis - y_top;
  if (plot_h < 2)
    return;

  int axis_x = x + 2;

  // Top arrow ^ and label
  grid_set(axis_x, y_top, "^", t->accent, (ColorRGB){0, 0, 0}, false, true,
           false);

  char top_label[32];
  if (max_val >= 100.0f) {
    snprintf(top_label, sizeof(top_label), "%3.0f %s", max_val, unit);
  } else {
    snprintf(top_label, sizeof(top_label), "%4.1f %s", max_val, unit);
  }
  grid_print(axis_x + 2, y_top, 10, top_label, t->text, false, false);

  // Vertical line |
  for (int r = y_top + 1; r < y_axis; r++) {
    grid_set(axis_x, r, "|", t->dim, (ColorRGB){0, 0, 0}, false, false, false);
  }

  // Horizontal axis line +--------->
  grid_set(axis_x, y_axis, "+", t->accent, (ColorRGB){0, 0, 0}, false, true,
           false);
  for (int c = axis_x + 1; c < x + w - 2; c++) {
    grid_set(c, y_axis, "-", t->dim, (ColorRGB){0, 0, 0}, false, false, false);
  }
  grid_set(x + w - 2, y_axis, ">", t->accent, (ColorRGB){0, 0, 0}, false, true,
           false);

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
          grid_set(dc, tr, ".", t->dim, (ColorRGB){0, 0, 0}, false, false,
                   true);
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
      if (val1 < 0.0f)
        val1 = 0.0f;
      float norm1 = val1 / max_val;
      if (norm1 > 1.0f)
        norm1 = 1.0f;
      float bar_h1 = norm1 * (float)plot_h;

      for (int r = 0; r < plot_h; r++) {
        int screen_r = y_axis - 1 - r;
        if (bar_h1 >= (float)(r + 1)) {
          grid_set(screen_c, screen_r, "█", col1, (ColorRGB){0, 0, 0}, false,
                   false, false);
        } else if (bar_h1 > (float)r && bar_h1 < (float)(r + 1)) {
          float frac = bar_h1 - (float)r;
          int b_idx = (int)(frac * 8.0f);
          if (b_idx < 1)
            b_idx = 1;
          if (b_idx > 8)
            b_idx = 8;
          grid_set(screen_c, screen_r, blocks[b_idx], col1, (ColorRGB){0, 0, 0},
                   false, false, false);
        }
      }

      // Series 2 (Dots/Points)
      int idx2 = count2 - data_w + c;
      if (idx2 >= 0 && idx2 < count2) {
        float val2 = hist2[idx2];
        if (val2 < 0.0f)
          val2 = 0.0f;
        float norm2 = val2 / max_val;
        if (norm2 > 1.0f)
          norm2 = 1.0f;
        int dot_r = (int)roundf(norm2 * (float)(plot_h - 1));
        int screen_r2 = y_axis - 1 - dot_r;
        grid_set(screen_c, screen_r2, "•", col2, (ColorRGB){0, 0, 0}, false,
                 true, false);
      }
    }
  }

  // Stats row under axis
  grid_set(axis_x, y_stat, "|", t->dim, (ColorRGB){0, 0, 0}, false, false,
           false);

  float last1 = (count1 > 0) ? hist1[count1 - 1] : 0.0f;
  float sum1 = 0.0f;
  int n1 = 0;
  for (int i = (count1 > data_w ? count1 - data_w : 0); i < count1; i++) {
    sum1 += hist1[i];
    n1++;
  }
  float avg1 = (n1 > 0) ? (sum1 / n1) : 0.0f;

  float last2 = (count2 > 0) ? hist2[count2 - 1] : 0.0f;
  float sum2 = 0.0f;
  int n2 = 0;
  for (int i = (count2 > data_w ? count2 - data_w : 0); i < count2; i++) {
    sum2 += hist2[i];
    n2++;
  }
  float avg2 = (n2 > 0) ? (sum2 / n2) : 0.0f;

  char stat_buf[256];
  snprintf(stat_buf, sizeof(stat_buf),
           "█ %s: last=%.1f avg=%.1f%s  • %s: last=%.1f avg=%.1f%s", name1,
           last1, avg1, unit, name2, last2, avg2, unit);
  grid_print(axis_x + 2, y_stat, w - 8, stat_buf, t->text, true, false);

  if (w >= 50) {
    const char *tag = "ttyplot 1.0";
    grid_print(x + w - (int)strlen(tag) - 3, y_stat, 16, tag, t->dim, false,
               true);
  }
}

// -------------------------------------------------------------
// 4 MAIN FEATURE READING CARDS
// -------------------------------------------------------------
static void draw_cpu_card(int x, int y, int w, int h,
                          const SystemTelemetry *telem, const Theme *t) {
  char extra[64] = "";
  if (telem->cpu_temp_avg > 0) {
    snprintf(extra, sizeof(extra), "%.1f°C", telem->cpu_temp_avg);
  }
  grid_box(x, y, w, h, "CPU MONITOR", extra, t->border, t->cpu_color, t->title);

  int cur_y = y + 1;
  grid_bar(x + 2, cur_y, w - 10, telem->cpu_total_pct, t->cpu_color, t->dim,
           t->text);
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
    snprintf(line2, sizeof(line2), "Load: %.2f %.2f %.2f", telem->load_avg[0],
             telem->load_avg[1], telem->load_avg[2]);
    grid_print(x + 2, cur_y, w - 4, line2, t->dim, false, false);
    cur_y++;
  }

  if (cur_y < y + h - 1) {
    char line3[128];
    snprintf(line3, sizeof(line3), "%d Cores (%dE + %dP)", telem->total_cores,
             telem->e_cores, telem->p_cores);
    grid_print(x + 2, cur_y, w - 4, line3, t->dim, false, false);
  }
}

static void draw_gpu_card(int x, int y, int w, int h,
                          const SystemTelemetry *telem, const Theme *t) {
  char extra[64] = "";
  if (telem->gpu_temp_avg > 0) {
    if (telem->has_discrete_gpu && telem->discrete_gpu_temp > 0) {
      snprintf(extra, sizeof(extra), "%.1f°C (dGPU %.1f°C)",
               telem->gpu_temp_avg, telem->discrete_gpu_temp);
    } else {
      snprintf(extra, sizeof(extra), "%.1f°C", telem->gpu_temp_avg);
    }
  } else if (telem->discrete_gpu_temp > 0) {
    snprintf(extra, sizeof(extra), "dGPU %.1f°C", telem->discrete_gpu_temp);
  }
  grid_box(x, y, w, h, "GPU & VRAM", extra, t->border, t->gpu_color, t->title);

  int cur_y = y + 1;
  grid_bar(x + 2, cur_y, w - 10, telem->gpu_device_pct, t->gpu_color, t->dim,
           t->text);
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
    if (telem->has_discrete_gpu && telem->discrete_gpu_name[0]) {
      snprintf(line3, sizeof(line3), "%s (Discrete)", telem->discrete_gpu_name);
    } else if (telem->gpu_name[0]) {
      snprintf(line3, sizeof(line3), "%d Cores • %s", telem->gpu_cores,
               telem->gpu_name);
    } else {
      snprintf(line3, sizeof(line3), "%d GPU Cores • Metal 3",
               telem->gpu_cores);
    }
    grid_print(x + 2, cur_y, w - 4, line3, t->dim, false, false);
  }
}

static void draw_mem_card(int x, int y, int w, int h,
                          const SystemTelemetry *telem, const Theme *t) {
  char used_str[32], tot_str[32];
  format_bytes(telem->mem_used_bytes, used_str, sizeof(used_str));
  format_bytes(telem->mem_total_bytes, tot_str, sizeof(tot_str));
  char extra[64];
  snprintf(extra, sizeof(extra), "%s", used_str);
  grid_box(x, y, w, h, "UNIFIED MEMORY", extra, t->border, t->mem_color,
           t->title);

  int cur_y = y + 1;
  grid_bar(x + 2, cur_y, w - 10, telem->mem_used_pct, t->mem_color, t->dim,
           t->text);
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

static void draw_net_card(int x, int y, int w, int h,
                          const SystemTelemetry *telem, const Theme *t) {
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
// View Mode Name and String Helpers
// -------------------------------------------------------------
const char *ui_view_mode_name(ViewMode mode) {
  switch (mode) {
  case VIEW_FULL:
    return "Full Dashboard";
  case VIEW_COMPACT:
    return "Compact Layout";
  case VIEW_ZEN:
    return "Zen Graphs";
  case VIEW_OVERVIEW:
    return "System Overview";
  case VIEW_CPU:
    return "CPU-Focused";
  case VIEW_GPU:
    return "GPU-Focused";
  case VIEW_MEM:
    return "Memory-Focused";
  case VIEW_NET:
    return "Network & Disk";
  default:
    return "Full Dashboard";
  }
}

ViewMode ui_view_mode_from_string(const char *str) {
  if (!str)
    return VIEW_FULL;
  if (strcasecmp(str, "full") == 0)
    return VIEW_FULL;
  if (strcasecmp(str, "compact") == 0)
    return VIEW_COMPACT;
  if (strcasecmp(str, "zen") == 0)
    return VIEW_ZEN;
  if (strcasecmp(str, "overview") == 0)
    return VIEW_OVERVIEW;
  if (strcasecmp(str, "cpu") == 0)
    return VIEW_CPU;
  if (strcasecmp(str, "gpu") == 0)
    return VIEW_GPU;
  if (strcasecmp(str, "mem") == 0 || strcasecmp(str, "memory") == 0)
    return VIEW_MEM;
  if (strcasecmp(str, "net") == 0 || strcasecmp(str, "network") == 0)
    return VIEW_NET;
  return VIEW_FULL;
}

// -------------------------------------------------------------
// Shared Layout Subcomponents
// -------------------------------------------------------------
static void draw_header_bar(const SystemTelemetry *telem, const Theme *t,
                            int W) {
  char hdr_left[256];
  if (telem->has_discrete_gpu && telem->discrete_gpu_name[0]) {
    snprintf(hdr_left, sizeof(hdr_left),
             "◈ MacPulse • %s • %d Cores (%dE+%dP) • %s • v1.0.0",
             telem->chip_name, telem->total_cores, telem->e_cores,
             telem->p_cores, telem->discrete_gpu_name);
  } else {
    snprintf(hdr_left, sizeof(hdr_left),
             "◈ MacPulse • %s • %d Cores (%dE+%dP) • %d GPU • v1.0.0",
             telem->chip_name, telem->total_cores, telem->e_cores,
             telem->p_cores, telem->gpu_cores);
  }

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
    grid_print_bg(right_x, 0, W - right_x - 1, hdr_right, t->header_fg,
                  t->header_bg, false);
  }
}

static void draw_footer_bar(UIState *state, const Theme *t, int W, int H,
                            bool show_process_table) {
  int foot_y = H - 1;
  for (int c = 0; c < W; c++) {
    grid_set(c, foot_y, " ", t->header_fg, t->header_bg, true, false, false);
  }

  char ftr[512];
  if (state->searching) {
    snprintf(ftr, sizeof(ftr), " Search: %s█  (Enter finish, Esc cancel)",
             state->search_buf);
  } else if (time(NULL) - state->status_msg_time < 3 && state->status_msg[0]) {
    snprintf(ftr, sizeof(ftr), " %s", state->status_msg);
  } else {
    const char *g_mode_str =
        (state->graph_mode == GRAPH_MODE_SPLIT)
            ? "Split"
            : ((state->graph_mode == GRAPH_MODE_STACKED)
                   ? "Stack"
                   : ((state->graph_mode == GRAPH_MODE_COMBINED) ? "Dual"
                                                                 : "Net"));
    const char *v_mode_str =
        (state->view_mode == VIEW_FULL)
            ? "Full"
            : ((state->view_mode == VIEW_COMPACT)
                   ? "Compact"
                   : ((state->view_mode == VIEW_ZEN)
                          ? "Zen"
                          : ((state->view_mode == VIEW_OVERVIEW)
                                 ? "Overview"
                                 : ((state->view_mode == VIEW_CPU)
                                        ? "CPU"
                                        : ((state->view_mode == VIEW_GPU)
                                               ? "GPU"
                                               : ((state->view_mode == VIEW_MEM)
                                                      ? "Memory"
                                                      : "Network"))))));
    snprintf(ftr, sizeof(ftr),
             " [q]Exit [t]Themes [v]Layout:%s [g]Graphs:%s [Tab]%s [c]CPU "
             "[m]Mem [/]Search [x]Kill [+/-]%.1fs",
             v_mode_str, g_mode_str,
             show_process_table ? "HideProcs" : "ShowProcs",
             (float)state->refresh_ms / 1000.0f);
  }
  grid_print_bg(1, foot_y, W - 2, ftr, t->header_fg, t->header_bg, true);
}

static void draw_process_table(UIState *state, const SystemTelemetry *telem,
                               const Theme *t, int proc_x, int proc_y,
                               int proc_w, int proc_h,
                               const char *title_override,
                               const char *hint_override) {
  if (proc_h < 4 || proc_w < 20)
    return;

  char proc_title[64];
  if (title_override && title_override[0]) {
    snprintf(proc_title, sizeof(proc_title), "%s", title_override);
  } else if (state->search_buf[0]) {
    snprintf(proc_title, sizeof(proc_title),
             "ACTIVITY MONITOR (%d/%d, /:\"%s\")", telem->procs.filtered_count,
             telem->procs.count, state->search_buf);
  } else {
    snprintf(proc_title, sizeof(proc_title), "ACTIVITY MONITOR (%d processes)",
             telem->procs.count);
  }

  const char *hint = hint_override ? hint_override
                                   : "↑/↓ scroll, / search, x kill, Tab toggle";
  grid_box(proc_x, proc_y, proc_w, proc_h, proc_title, hint, t->border,
           t->title, t->dim);

  int tbl_y = proc_y + 1;
  char col_hdr[256];
  snprintf(col_hdr, sizeof(col_hdr),
           "%-6s  %-12s  %-9s  %5s  %5s  %5s  %-7s  %s", "PID", "USER", "RES",
           "%CPU", "%GPU", "%MEM", "TIME", "COMMAND");
  grid_print(proc_x + 2, tbl_y, proc_w - 4, col_hdr, t->accent, true, false);

  int avail_rows = proc_h - 3;
  if (avail_rows < 1)
    avail_rows = 1;

  if (state->proc_selected < 0)
    state->proc_selected = 0;
  if (state->proc_selected >= telem->procs.filtered_count) {
    state->proc_selected =
        telem->procs.filtered_count > 0 ? telem->procs.filtered_count - 1 : 0;
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
      snprintf(row_line, sizeof(row_line),
               "%-6d  %-12.12s  %-9s  %4.1f%%  %4.1f%%  %4.1f%%  %-7s  %s",
               pe->pid, pe->username, res_str, pe->cpu_pct, pe->gpu_pct,
               pe->mem_pct, time_s, pe->name);

      bool is_sel = (idx == state->proc_selected);
      if (is_sel) {
        for (int c = proc_x + 1; c < proc_x + proc_w - 1; c++) {
          grid_set(c, row_y, " ", t->proc_sel_fg, t->proc_sel_bg, true, false,
                   false);
        }
        grid_print_bg(proc_x + 2, row_y, proc_w - 4, row_line, t->proc_sel_fg,
                      t->proc_sel_bg, true);
      } else {
        ColorRGB cpu_col = (pe->cpu_pct > 50.0f)
                               ? t->crit
                               : (pe->cpu_pct > 15.0f ? t->warn : t->text);
        grid_print(proc_x + 2, row_y, proc_w - 4, row_line, cpu_col, false,
                   false);
      }
    }
  }
}

static void draw_history_graphs(UIState *state, const SystemTelemetry *telem,
                                const Theme *t, int graph_x, int graph_y,
                                int graph_w, int graph_h) {
  if (graph_h < 4 || graph_w < 20)
    return;

  switch (state->graph_mode) {
  case GRAPH_MODE_SPLIT: {
    if (graph_w >= 90) {
      int left_w = graph_w / 2;
      int right_w = graph_w - left_w;
      grid_ttyplot(graph_x, graph_y, left_w, graph_h, "CPU USAGE HISTORY",
                   "Apple Silicon Cores", telem->history_cpu,
                   telem->history_cpu_count, 100.0f, "%", t->cpu_color, t);
      grid_ttyplot(graph_x + left_w, graph_y, right_w, graph_h,
                   "GPU USAGE HISTORY", "IOAccelerator", telem->history_gpu,
                   telem->history_gpu_count, 100.0f, "%", t->gpu_color, t);
    } else {
      int gh1 = graph_h / 2;
      int gh2 = graph_h - gh1;
      grid_ttyplot(graph_x, graph_y, graph_w, gh1, "CPU USAGE HISTORY", NULL,
                   telem->history_cpu, telem->history_cpu_count, 100.0f, "%",
                   t->cpu_color, t);
      grid_ttyplot(graph_x, graph_y + gh1, graph_w, gh2, "GPU USAGE HISTORY",
                   NULL, telem->history_gpu, telem->history_gpu_count, 100.0f,
                   "%", t->gpu_color, t);
    }
    break;
  }
  case GRAPH_MODE_STACKED: {
    int gh1 = graph_h / 2;
    int gh2 = graph_h - gh1;
    grid_ttyplot(graph_x, graph_y, graph_w, gh1, "CPU USAGE HISTORY",
                 "Apple Silicon Cores", telem->history_cpu,
                 telem->history_cpu_count, 100.0f, "%", t->cpu_color, t);
    grid_ttyplot(graph_x, graph_y + gh1, graph_w, gh2, "GPU USAGE HISTORY",
                 "IOAccelerator", telem->history_gpu, telem->history_gpu_count,
                 100.0f, "%", t->gpu_color, t);
    break;
  }
  case GRAPH_MODE_COMBINED: {
    grid_ttyplot_dual(graph_x, graph_y, graph_w, graph_h,
                      "CPU & GPU USAGE DUAL-SERIES HISTORY", "█ CPU • GPU",
                      telem->history_cpu, telem->history_cpu_count, "CPU",
                      t->cpu_color, telem->history_gpu,
                      telem->history_gpu_count, "GPU", t->gpu_color, 100.0f,
                      "%", t);
    break;
  }
  case GRAPH_MODE_NET: {
    grid_ttyplot_dual(graph_x, graph_y, graph_w, graph_h,
                      "NETWORK BANDWIDTH THROUGHPUT HISTORY", "▼ Down • Up",
                      telem->history_net_down, telem->history_net_count, "Down",
                      t->accent, telem->history_net_up,
                      telem->history_net_count, "Up", t->warn, 0.0f, "KB/s", t);
    break;
  }
  }
}

// -------------------------------------------------------------
// Layout 1: FULL DASHBOARD (Default classic layout)
// -------------------------------------------------------------
static void render_full_view(UIState *state, const SystemTelemetry *telem,
                             const Theme *t, int W, int H) {
  draw_header_bar(telem, t, W);

  int card_h = 6;
  int card_bottom_y = 1;

  if (W >= 108) {
    int col_w = W / 4;
    draw_cpu_card(0, 1, col_w, card_h, telem, t);
    draw_gpu_card(col_w, 1, col_w, card_h, telem, t);
    draw_mem_card(col_w * 2, 1, col_w, card_h, telem, t);
    draw_net_card(col_w * 3, 1, W - col_w * 3, card_h, telem, t);
    card_bottom_y = 1 + card_h;
  } else {
    int half_w = W / 2;
    int rem_w = W - half_w;
    draw_cpu_card(0, 1, half_w, card_h, telem, t);
    draw_gpu_card(half_w, 1, rem_w, card_h, telem, t);
    draw_mem_card(0, 1 + card_h, half_w, card_h, telem, t);
    draw_net_card(half_w, 1 + card_h, rem_w, card_h, telem, t);
    card_bottom_y = 1 + card_h * 2;
  }

  int total_avail = (H - 1) - card_bottom_y;
  if (total_avail < 6)
    total_avail = 6;

  bool show_process_table = state->show_procs && (total_avail >= 16);
  int graph_y = card_bottom_y;
  int graph_h = show_process_table ? (total_avail * 58 / 100) : total_avail;
  if (graph_h < 7)
    graph_h = 7;
  if (show_process_table && (total_avail - graph_h < 6)) {
    graph_h = total_avail - 6;
  }

  draw_history_graphs(state, telem, t, 0, graph_y, W, graph_h);

  if (show_process_table) {
    int proc_y = graph_y + graph_h;
    int proc_h = (H - 1) - proc_y;
    draw_process_table(state, telem, t, 0, proc_y, W, proc_h, NULL, NULL);
  }

  draw_footer_bar(state, t, W, H, show_process_table);
}

// -------------------------------------------------------------
// Layout 2: COMPACT VIEW (High-density mini metric strip)
// -------------------------------------------------------------
static void render_compact_view(UIState *state, const SystemTelemetry *telem,
                                const Theme *t, int W, int H) {
  draw_header_bar(telem, t, W);

  int card_h = 4;
  int card_bottom_y = 1 + card_h;

  if (W >= 104) {
    int col_w = W / 4;
    // 1. CPU Mini
    char c_extra[32] = "";
    if (telem->cpu_temp_avg > 0)
      snprintf(c_extra, sizeof(c_extra), "%.1f°C", telem->cpu_temp_avg);
    grid_box(0, 1, col_w, card_h, "CPU MONITOR", c_extra, t->border,
             t->cpu_color, t->title);
    grid_bar(2, 2, col_w - 10, telem->cpu_total_pct, t->cpu_color, t->dim,
             t->text);
    char c_sub[64];
    snprintf(c_sub, sizeof(c_sub), "Load: %.2f • U:%2.0f%% S:%2.0f%%",
             telem->load_avg[0], telem->cpu_user_pct, telem->cpu_sys_pct);
    grid_print(2, 3, col_w - 4, c_sub, t->dim, false, false);

    // 2. GPU Mini
    char g_extra[32] = "";
    if (telem->gpu_temp_avg > 0)
      snprintf(g_extra, sizeof(g_extra), "%.1f°C", telem->gpu_temp_avg);
    grid_box(col_w, 1, col_w, card_h, "GPU & VRAM", g_extra, t->border,
             t->gpu_color, t->title);
    grid_bar(col_w + 2, 2, col_w - 10, telem->gpu_device_pct, t->gpu_color,
             t->dim, t->text);
    char g_sub[64], in_v[32];
    format_bytes(telem->gpu_in_use_bytes, in_v, sizeof(in_v));
    snprintf(g_sub, sizeof(g_sub), "Ren: %2.0f%% Til: %2.0f%% • %s",
             telem->gpu_renderer_pct, telem->gpu_tiler_pct, in_v);
    grid_print(col_w + 2, 3, col_w - 4, g_sub, t->dim, false, false);

    // 3. MEM Mini
    char m_extra[32] = "", u_mem[32];
    format_bytes(telem->mem_used_bytes, u_mem, sizeof(u_mem));
    snprintf(m_extra, sizeof(m_extra), "%s", u_mem);
    grid_box(col_w * 2, 1, col_w, card_h, "UNIFIED MEMORY", m_extra, t->border,
             t->mem_color, t->title);
    grid_bar(col_w * 2 + 2, 2, col_w - 10, telem->mem_used_pct, t->mem_color,
             t->dim, t->text);
    char m_sub[64], f_mem[32];
    format_bytes(telem->mem_free_bytes, f_mem, sizeof(f_mem));
    snprintf(m_sub, sizeof(m_sub), "Free: %s • Swap: %s", f_mem,
             telem->swap_used_bytes > 0 ? "Active" : "0B");
    grid_print(col_w * 2 + 2, 3, col_w - 4, m_sub, t->dim, false, false);

    // 4. NET/DISK Mini
    int rem_w = W - col_w * 3;
    grid_box(col_w * 3, 1, rem_w, card_h, "NETWORK & DISK", NULL, t->border,
             t->accent, t->title);
    char d_spd[32], u_spd[32];
    format_speed(telem->net_download_bps, d_spd, sizeof(d_spd));
    format_speed(telem->net_upload_bps, u_spd, sizeof(u_spd));
    char n_line[64];
    snprintf(n_line, sizeof(n_line), "▼ %s  ▲ %s", d_spd, u_spd);
    grid_print(col_w * 3 + 2, 2, rem_w - 4, n_line, t->accent, true, false);
    char r_s[32], w_s[32], disk_sub[64];
    format_speed(telem->disk_read_bps, r_s, sizeof(r_s));
    format_speed(telem->disk_write_bps, w_s, sizeof(w_s));
    snprintf(disk_sub, sizeof(disk_sub), "Disk: R %s W %s", r_s, w_s);
    grid_print(col_w * 3 + 2, 3, rem_w - 4, disk_sub, t->dim, false, false);
  } else {
    // Fallback 2-column compact strip
    int hw = W / 2;
    draw_cpu_card(0, 1, hw, 5, telem, t);
    draw_gpu_card(hw, 1, W - hw, 5, telem, t);
    card_bottom_y = 6;
  }

  int total_avail = (H - 1) - card_bottom_y;
  if (total_avail < 6)
    total_avail = 6;

  bool show_process_table = state->show_procs && (total_avail >= 14);
  int graph_h = show_process_table ? 8 : total_avail;
  if (show_process_table && (total_avail - graph_h < 6)) {
    graph_h = total_avail - 6;
  }

  draw_history_graphs(state, telem, t, 0, card_bottom_y, W, graph_h);

  if (show_process_table) {
    int proc_y = card_bottom_y + graph_h;
    int proc_h = (H - 1) - proc_y;
    draw_process_table(state, telem, t, 0, proc_y, W, proc_h, NULL, NULL);
  }

  draw_footer_bar(state, t, W, H, show_process_table);
}

// -------------------------------------------------------------
// Layout 3: ZEN VIEW (Maximized distraction-free graphs)
// -------------------------------------------------------------
static void render_zen_view(UIState *state, const SystemTelemetry *telem,
                            const Theme *t, int W, int H) {
  // 1. Slim Zen top header
  time_t raw_time = time(NULL);
  struct tm *time_info = localtime(&raw_time);
  char time_str[32];
  strftime(time_str, sizeof(time_str), "%H:%M:%S", time_info);

  char zen_hdr[256];
  snprintf(zen_hdr, sizeof(zen_hdr),
           "◈ MacPulse Zen • %s • CPU %.1f%% (%.1f°C) • GPU %.1f%% (%.1f°C) • "
           "RAM %.1f%% • Up: %s • %s",
           telem->chip_name, telem->cpu_total_pct, telem->cpu_temp_avg,
           telem->gpu_device_pct, telem->gpu_temp_avg, telem->mem_used_pct,
           telem->uptime_str, time_str);

  for (int c = 0; c < W; c++) {
    grid_set(c, 0, " ", t->header_fg, t->header_bg, true, false, false);
  }
  grid_print_bg(1, 0, W - 2, zen_hdr, t->header_fg, t->header_bg, true);

  // 2. Maximized Graph spanning the screen
  int graph_h =
      (H - 1) - 2; // Leave row 0 for header, H-2 for ticker, H-1 for footer
  if (graph_h < 6)
    graph_h = 6;
  draw_history_graphs(state, telem, t, 0, 1, W, graph_h);

  // 3. Sleek real-time telemetry ticker above footer
  int ticker_y = H - 2;
  for (int c = 0; c < W; c++) {
    grid_set(c, ticker_y, " ", t->text, (ColorRGB){0, 0, 0}, false, false,
             false);
  }

  char u_mem[32], t_mem[32], d_spd[32], u_spd[32], r_spd[32], w_spd[32];
  format_bytes(telem->mem_used_bytes, u_mem, sizeof(u_mem));
  format_bytes(telem->mem_total_bytes, t_mem, sizeof(t_mem));
  format_speed(telem->net_download_bps, d_spd, sizeof(d_spd));
  format_speed(telem->net_upload_bps, u_spd, sizeof(u_spd));
  format_speed(telem->disk_read_bps, r_spd, sizeof(r_spd));
  format_speed(telem->disk_write_bps, w_spd, sizeof(w_spd));

  char ticker[512];
  snprintf(
      ticker, sizeof(ticker),
      " CPU: %4.1f%% (U:%2.0f%% S:%2.0f%%) │ GPU: %4.1f%% (R:%2.0f%% "
      "T:%2.0f%%) │ RAM: %4.1f%% (%s/%s) │ Net: ▼ %s ▲ %s │ Disk: R %s W %s",
      telem->cpu_total_pct, telem->cpu_user_pct, telem->cpu_sys_pct,
      telem->gpu_device_pct, telem->gpu_renderer_pct, telem->gpu_tiler_pct,
      telem->mem_used_pct, u_mem, t_mem, d_spd, u_spd, r_spd, w_spd);
  grid_print(1, ticker_y, W - 2, ticker, t->dim, false, false);

  // 4. Footer
  draw_footer_bar(state, t, W, H, false);
}

// -------------------------------------------------------------
// Layout 4: SYSTEM OVERVIEW (Hardware specs & combined vitals)
// -------------------------------------------------------------
static void render_overview_view(UIState *state, const SystemTelemetry *telem,
                                 const Theme *t, int W, int H) {
  draw_header_bar(telem, t, W);

  // 1. Hardware & System Health Banner (height 4 rows)
  int banner_h = 4;
  int left_w = (W >= 96) ? (W * 55 / 100) : W;
  grid_box(0, 1, left_w, banner_h, "SYSTEM ARCHITECTURE & HARDWARE", NULL,
           t->border, t->title, t->dim);

  char line_hw1[128], line_hw2[128];
  snprintf(line_hw1, sizeof(line_hw1), "Hardware:  %s (%s) • macOS %s",
           telem->model_name, telem->chip_name, telem->os_version);
  snprintf(line_hw2, sizeof(line_hw2),
           "Topology:  %d Cores (%dE+%dP) • %d GPU Cores • %s",
           telem->total_cores, telem->e_cores, telem->p_cores, telem->gpu_cores,
           telem->has_discrete_gpu ? telem->discrete_gpu_name
                                   : "Apple Unified SoC");
  grid_print(2, 2, left_w - 4, line_hw1, t->text, true, false);
  grid_print(2, 3, left_w - 4, line_hw2, t->dim, false, false);

  if (W >= 96) {
    int right_w = W - left_w;
    grid_box(left_w, 1, right_w, banner_h, "VITALS & THERMAL HEALTH", NULL,
             t->border, t->accent, t->dim);
    char line_vh1[128], line_vh2[128];
    snprintf(line_vh1, sizeof(line_vh1),
             "Thermal:   %s • Max SoC: %.1f°C • Die: CPU %.1f°C / GPU %.1f°C",
             telem->thermal_state_str, telem->soc_temp_max, telem->cpu_temp_avg,
             telem->gpu_temp_avg);
    snprintf(line_vh2, sizeof(line_vh2),
             "Power:     %s • Load Avg: %.2f %.2f %.2f",
             telem->has_battery
                 ? (telem->battery_charging ? "Battery [CHG]" : "Battery")
                 : "AC Power",
             telem->load_avg[0], telem->load_avg[1], telem->load_avg[2]);
    grid_print(left_w + 2, 2, right_w - 4, line_vh1, t->text, true, false);
    grid_print(left_w + 2, 3, right_w - 4, line_vh2, t->dim, false, false);
  }

  // 2. 4 Feature Cards (height 6 rows)
  int cards_y = 1 + banner_h;
  int card_h = 6;
  if (W >= 108) {
    int col_w = W / 4;
    draw_cpu_card(0, cards_y, col_w, card_h, telem, t);
    draw_gpu_card(col_w, cards_y, col_w, card_h, telem, t);
    draw_mem_card(col_w * 2, cards_y, col_w, card_h, telem, t);
    draw_net_card(col_w * 3, cards_y, W - col_w * 3, card_h, telem, t);
  } else {
    int half_w = W / 2;
    draw_cpu_card(0, cards_y, half_w, card_h, telem, t);
    draw_gpu_card(half_w, cards_y, W - half_w, card_h, telem, t);
    draw_mem_card(0, cards_y + card_h, half_w, card_h, telem, t);
    draw_net_card(half_w, cards_y + card_h, W - half_w, card_h, telem, t);
  }

  int bottom_y = cards_y + card_h * (W >= 108 ? 1 : 2);
  int avail_h = (H - 1) - bottom_y;
  if (avail_h < 6)
    avail_h = 6;

  bool show_process_table = state->show_procs && (avail_h >= 14);

  if (W >= 108 && avail_h >= 10) {
    // Split bottom area: Left Graph, Right Top Processes
    int graph_w = W * 56 / 100;
    int proc_w = W - graph_w;
    draw_history_graphs(state, telem, t, 0, bottom_y, graph_w, avail_h);
    draw_process_table(state, telem, t, graph_w, bottom_y, proc_w, avail_h,
                       "TOP RESOURCE CONSUMERS", "/ search, x kill");
  } else {
    int gh = show_process_table ? (avail_h * 55 / 100) : avail_h;
    if (gh < 6)
      gh = 6;
    draw_history_graphs(state, telem, t, 0, bottom_y, W, gh);
    if (show_process_table) {
      int py = bottom_y + gh;
      draw_process_table(state, telem, t, 0, py, W, (H - 1) - py, NULL, NULL);
    }
  }

  draw_footer_bar(state, t, W, H, show_process_table);
}

// -------------------------------------------------------------
// Layout 5: CPU-FOCUSED VIEW (Per-core meters & CPU graph)
// -------------------------------------------------------------
static void render_cpu_view(UIState *state, const SystemTelemetry *telem,
                            const Theme *t, int W, int H) {
  draw_header_bar(telem, t, W);

  int top_h = 9;
  int left_w = (W >= 96) ? 46 : (W / 2);
  if (left_w < 32)
    left_w = 32;
  int right_w = W - left_w;

  // 1. CPU Overview Card
  char c_extra[32] = "";
  if (telem->cpu_temp_avg > 0)
    snprintf(c_extra, sizeof(c_extra), "%.1f°C", telem->cpu_temp_avg);
  grid_box(0, 1, left_w, top_h, "CPU OVERVIEW & LOAD", c_extra, t->border,
           t->cpu_color, t->title);

  grid_bar(2, 2, left_w - 10, telem->cpu_total_pct, t->cpu_color, t->dim,
           t->text);

  char l1[128], l2[128], l3[128], l4[128], l5[128], l6[128];
  snprintf(l1, sizeof(l1), "User Load:     %5.1f%%   Sys Load: %5.1f%%",
           telem->cpu_user_pct, telem->cpu_sys_pct);
  snprintf(l2, sizeof(l2), "Idle CPU:      %5.1f%%   Total:    %5.1f%%",
           telem->cpu_idle_pct, telem->cpu_total_pct);
  snprintf(l3, sizeof(l3), "Load Averages: 1m: %.2f  5m: %.2f  15m: %.2f",
           telem->load_avg[0], telem->load_avg[1], telem->load_avg[2]);
  snprintf(l4, sizeof(l4), "Thermals:      Avg: %.1f°C  Max: %.1f°C",
           telem->cpu_temp_avg, telem->cpu_temp_max);
  snprintf(l5, sizeof(l5), "Die Sensors:   SoC: %.1f°C  State: %s",
           telem->soc_temp_max, telem->thermal_state_str);
  snprintf(l6, sizeof(l6), "Topology:      %d Cores (%d Perf + %d Effic)",
           telem->total_cores, telem->p_cores, telem->e_cores);

  grid_print(2, 3, left_w - 4, l1, t->dim, false, false);
  grid_print(2, 4, left_w - 4, l2, t->dim, false, false);
  grid_print(2, 5, left_w - 4, l3, t->dim, false, false);
  grid_print(2, 6, left_w - 4, l4, t->text, true, false);
  grid_print(2, 7, left_w - 4, l5, t->dim, false, false);
  grid_print(2, 8, left_w - 4, l6, t->text, true, false);

  // 2. Per-Core Meter Grid
  grid_box(left_w, 1, right_w, top_h, "PER-CORE UTILIZATION METER",
           "P-Cores & E-Cores", t->border, t->accent, t->dim);

  int inner_w = right_w - 4;
  int num_cols =
      (inner_w >= 78) ? 4 : ((inner_w >= 52) ? 3 : ((inner_w >= 32) ? 2 : 1));
  int col_width = inner_w / num_cols;
  int max_cores_display = (top_h - 2) * num_cols;
  int total_c = telem->total_cores < MAX_CORES ? telem->total_cores : MAX_CORES;
  if (total_c > max_cores_display)
    total_c = max_cores_display;

  for (int i = 0; i < total_c; i++) {
    int c_col = i % num_cols;
    int c_row = i / num_cols;
    int cell_x = left_w + 2 + c_col * col_width;
    int cell_y = 2 + c_row;

    char c_lbl[16];
    bool is_p_core = (telem->p_cores > 0 && i < telem->p_cores);
    if (telem->p_cores > 0 && telem->e_cores > 0) {
      snprintf(c_lbl, sizeof(c_lbl), "%s%02d", is_p_core ? "P" : "E",
               is_p_core ? i + 1 : i - telem->p_cores + 1);
    } else {
      snprintf(c_lbl, sizeof(c_lbl), "C%02d", i + 1);
    }

    grid_print(cell_x, cell_y, 4, c_lbl, is_p_core ? t->title : t->dim,
               is_p_core, false);
    int bar_w = col_width - 11;
    if (bar_w < 4)
      bar_w = 4;
    grid_bar(cell_x + 4, cell_y, bar_w, telem->core_pct[i],
             is_p_core ? t->cpu_color : t->accent2, t->dim, t->text);
  }

  // 3. Full-Width CPU Usage Graph & Process Table
  int avail_h = (H - 1) - (1 + top_h);
  if (avail_h < 6)
    avail_h = 6;
  bool show_process_table = state->show_procs && (avail_h >= 14);
  int graph_h = show_process_table ? (avail_h * 52 / 100) : avail_h;
  if (graph_h < 7)
    graph_h = 7;
  if (show_process_table && (avail_h - graph_h < 6)) {
    graph_h = avail_h - 6;
  }

  grid_ttyplot(0, 1 + top_h, W, graph_h, "CPU USAGE HISTORY",
               "Apple Silicon Cores", telem->history_cpu,
               telem->history_cpu_count, 100.0f, "%", t->cpu_color, t);

  if (show_process_table) {
    int py = 1 + top_h + graph_h;
    int ph = (H - 1) - py;
    draw_process_table(state, telem, t, 0, py, W, ph,
                       "ACTIVITY MONITOR • CPU PROCESSES",
                       "Sorted by %CPU (descending)");
  }

  draw_footer_bar(state, t, W, H, show_process_table);
}

// -------------------------------------------------------------
// Layout 6: GPU-FOCUSED VIEW (GPU engines, VRAM, and GPU graph)
// -------------------------------------------------------------
static void render_gpu_view(UIState *state, const SystemTelemetry *telem,
                            const Theme *t, int W, int H) {
  draw_header_bar(telem, t, W);

  int top_h = 9;
  int left_w = W / 2;
  int right_w = W - left_w;

  // 1. GPU Engine Card
  char g_extra[64] = "";
  if (telem->gpu_temp_avg > 0) {
    if (telem->has_discrete_gpu && telem->discrete_gpu_temp > 0) {
      snprintf(g_extra, sizeof(g_extra), "%.1f°C (dGPU %.1f°C)",
               telem->gpu_temp_avg, telem->discrete_gpu_temp);
    } else {
      snprintf(g_extra, sizeof(g_extra), "%.1f°C", telem->gpu_temp_avg);
    }
  } else if (telem->discrete_gpu_temp > 0) {
    snprintf(g_extra, sizeof(g_extra), "dGPU %.1f°C", telem->discrete_gpu_temp);
  }
  grid_box(0, 1, left_w, top_h, "GPU ENGINE & HARDWARE", g_extra, t->border,
           t->gpu_color, t->title);

  grid_bar(2, 2, left_w - 10, telem->gpu_device_pct, t->gpu_color, t->dim,
           t->text);

  char gl1[128], gl2[128], gl3[128], gl4[128], gl5[128], gl6[128];
  snprintf(gl1, sizeof(gl1), "Device Model:   %s", telem->gpu_name);
  snprintf(gl2, sizeof(gl2),
           "Engine Stats:   Renderer: %4.1f%%  Tiler: %4.1f%%",
           telem->gpu_renderer_pct, telem->gpu_tiler_pct);
  snprintf(gl3, sizeof(gl3), "Thermal Die:    Avg: %.1f°C  Max: %.1f°C",
           telem->gpu_temp_avg, telem->gpu_temp_max);
  if (telem->has_discrete_gpu) {
    snprintf(gl4, sizeof(gl4), "Discrete GPU:   %s (Temp: %.1f°C)",
             telem->discrete_gpu_name, telem->discrete_gpu_temp);
  } else {
    snprintf(gl4, sizeof(gl4),
             "Discrete GPU:   None (Unified Apple Silicon SoC)");
  }
  snprintf(gl5, sizeof(gl5),
           "Compute Cores:  %d GPU Cores • Apple Metal 3 Architecture",
           telem->gpu_cores);
  snprintf(gl6, sizeof(gl6),
           "IOKit Driver:   Apple IOAcceleratorPerformanceStatistics");

  grid_print(2, 3, left_w - 4, gl1, t->text, true, false);
  grid_print(2, 4, left_w - 4, gl2, t->dim, false, false);
  grid_print(2, 5, left_w - 4, gl3, t->dim, false, false);
  grid_print(2, 6, left_w - 4, gl4, t->accent, true, false);
  grid_print(2, 7, left_w - 4, gl5, t->dim, false, false);
  grid_print(2, 8, left_w - 4, gl6, t->text, false, true);

  // 2. VRAM & Graphics Memory Card
  char in_vram[32], alloc_vram[32];
  format_bytes(telem->gpu_in_use_bytes, in_vram, sizeof(in_vram));
  format_bytes(telem->gpu_alloc_bytes, alloc_vram, sizeof(alloc_vram));
  char v_extra[64];
  snprintf(v_extra, sizeof(v_extra), "%s in-use", in_vram);
  grid_box(left_w, 1, right_w, top_h, "GRAPHICS VRAM & MEMORY", v_extra,
           t->border, t->accent, t->title);

  float vram_pct = telem->gpu_alloc_bytes > 0
                       ? ((float)telem->gpu_in_use_bytes /
                          (float)telem->gpu_alloc_bytes * 100.0f)
                       : 0.0f;
  grid_bar(left_w + 2, 2, right_w - 10, vram_pct, t->accent, t->dim, t->text);

  char vl1[128], vl2[128], vl3[128], vl4[128], vl5[128], vl6[128];
  snprintf(vl1, sizeof(vl1), "In-Use VRAM:    %s", in_vram);
  snprintf(vl2, sizeof(vl2), "Allocated VRAM: %s", alloc_vram);
  snprintf(vl3, sizeof(vl3), "Architecture:   %s",
           telem->has_discrete_gpu ? "Dedicated PCIe VRAM"
                                   : "Unified Memory Architecture");
  snprintf(vl4, sizeof(vl4),
           "Buffer Mode:    Zero-Copy Metal Shared System Buffer");
  snprintf(vl5, sizeof(vl5),
           "Pipeline State: Hardware Ray Tracing & Mesh Shaders");
  snprintf(vl6, sizeof(vl6), "Host Device:    %s", telem->model_name);

  grid_print(left_w + 2, 3, right_w - 4, vl1, t->text, true, false);
  grid_print(left_w + 2, 4, right_w - 4, vl2, t->dim, false, false);
  grid_print(left_w + 2, 5, right_w - 4, vl3, t->accent, true, false);
  grid_print(left_w + 2, 6, right_w - 4, vl4, t->dim, false, false);
  grid_print(left_w + 2, 7, right_w - 4, vl5, t->dim, false, false);
  grid_print(left_w + 2, 8, right_w - 4, vl6, t->text, false, true);

  // 3. Full-Width GPU Usage Graph & Process Table
  int avail_h = (H - 1) - (1 + top_h);
  if (avail_h < 6)
    avail_h = 6;
  bool show_process_table = state->show_procs && (avail_h >= 14);
  int graph_h = show_process_table ? (avail_h * 52 / 100) : avail_h;
  if (graph_h < 7)
    graph_h = 7;
  if (show_process_table && (avail_h - graph_h < 6)) {
    graph_h = avail_h - 6;
  }

  grid_ttyplot(0, 1 + top_h, W, graph_h, "GPU USAGE HISTORY",
               "IOAccelerator Engine", telem->history_gpu,
               telem->history_gpu_count, 100.0f, "%", t->gpu_color, t);

  if (show_process_table) {
    int py = 1 + top_h + graph_h;
    int ph = (H - 1) - py;
    draw_process_table(state, telem, t, 0, py, W, ph,
                       "ACTIVITY MONITOR • GPU & UI PROCESSES",
                       "WindowServer / Renderers / Compute");
  }

  draw_footer_bar(state, t, W, H, show_process_table);
}

// -------------------------------------------------------------
// Layout 7: MEMORY-FOCUSED VIEW (Unified RAM map & memory graph)
// -------------------------------------------------------------
static void render_mem_view(UIState *state, const SystemTelemetry *telem,
                            const Theme *t, int W, int H) {
  draw_header_bar(telem, t, W);

  int top_h = 9;
  int left_w = W / 2;
  int right_w = W - left_w;

  // 1. Unified Memory Status Card
  char u_str[32], tot_str[32], f_str[32], s_u_str[32], s_t_str[32];
  format_bytes(telem->mem_used_bytes, u_str, sizeof(u_str));
  format_bytes(telem->mem_total_bytes, tot_str, sizeof(tot_str));
  format_bytes(telem->mem_free_bytes, f_str, sizeof(f_str));
  format_bytes(telem->swap_used_bytes, s_u_str, sizeof(s_u_str));
  format_bytes(telem->swap_total_bytes, s_t_str, sizeof(s_t_str));

  grid_box(0, 1, left_w, top_h, "UNIFIED MEMORY STATUS", u_str, t->border,
           t->mem_color, t->title);
  grid_bar(2, 2, left_w - 10, telem->mem_used_pct, t->mem_color, t->dim,
           t->text);

  char ml1[128], ml2[128], ml3[128], ml4[128];
  snprintf(ml1, sizeof(ml1), "Total RAM:       %s", tot_str);
  snprintf(ml2, sizeof(ml2), "Used Memory:     %s (%4.1f%%)", u_str,
           telem->mem_used_pct);
  snprintf(ml3, sizeof(ml3), "Free Memory:     %s", f_str);
  snprintf(ml4, sizeof(ml4), "Swap Storage:    Used: %s / Total: %s", s_u_str,
           s_t_str);

  grid_print(2, 3, left_w - 4, ml1, t->text, true, false);
  grid_print(2, 4, left_w - 4, ml2, t->dim, false, false);
  grid_print(2, 5, left_w - 4, ml3, t->dim, false, false);
  grid_print(2, 6, left_w - 4, ml4, t->text, false, false);

  // Multi-segment visual memory composition bar on row 7
  int bar_start = 2;
  int bar_len = left_w - 4;
  float tot_f = (float)telem->mem_total_bytes;
  if (tot_f > 0 && bar_len > 10) {
    int app_len = (int)((float)telem->mem_active_bytes / tot_f * bar_len);
    int wire_len = (int)((float)telem->mem_wired_bytes / tot_f * bar_len);
    int comp_len = (int)((float)telem->mem_compressed_bytes / tot_f * bar_len);
    int cache_len = (int)((float)telem->mem_inactive_bytes / tot_f * bar_len);

    int cur_x = bar_start;
    for (int i = 0; i < app_len && cur_x < bar_start + bar_len; i++, cur_x++)
      grid_set(cur_x, 7, "█", t->accent, (ColorRGB){0, 0, 0}, false, false,
               false);
    for (int i = 0; i < wire_len && cur_x < bar_start + bar_len; i++, cur_x++)
      grid_set(cur_x, 7, "█", t->gpu_color, (ColorRGB){0, 0, 0}, false, false,
               false);
    for (int i = 0; i < comp_len && cur_x < bar_start + bar_len; i++, cur_x++)
      grid_set(cur_x, 7, "█", t->warn, (ColorRGB){0, 0, 0}, false, false,
               false);
    for (int i = 0; i < cache_len && cur_x < bar_start + bar_len; i++, cur_x++)
      grid_set(cur_x, 7, "█", t->dim, (ColorRGB){0, 0, 0}, false, false, false);
    while (cur_x < bar_start + bar_len) {
      grid_set(cur_x, 7, "░", t->dim, (ColorRGB){0, 0, 0}, false, false, false);
      cur_x++;
    }
  }
  grid_print(2, 8, left_w - 4, "■ App  ■ Wired  ■ Comp  ■ Cache  ░ Free",
             t->dim, false, true);

  // 2. Mach VM Memory Subsystem Card
  char app_s[32], wired_s[32], comp_s[32], cache_s[32], free_s[32];
  format_bytes(telem->mem_active_bytes, app_s, sizeof(app_s));
  format_bytes(telem->mem_wired_bytes, wired_s, sizeof(wired_s));
  format_bytes(telem->mem_compressed_bytes, comp_s, sizeof(comp_s));
  format_bytes(telem->mem_inactive_bytes, cache_s, sizeof(cache_s));
  format_bytes(telem->mem_free_bytes, free_s, sizeof(free_s));

  grid_box(left_w, 1, right_w, top_h, "MACH VM MEMORY SUBSYSTEM", NULL,
           t->border, t->title, t->dim);

  char vml1[128], vml2[128], vml3[128], vml4[128], vml5[128], vml6[128];
  snprintf(vml1, sizeof(vml1),
           "App Memory:        %s  (active processes & memory heaps)", app_s);
  snprintf(vml2, sizeof(vml2),
           "Wired Memory:      %s  (kernel non-pageable memory)", wired_s);
  snprintf(vml3, sizeof(vml3),
           "Compressed Memory: %s  (fast in-RAM compressor pool)", comp_s);
  snprintf(vml4, sizeof(vml4), "Cached Pages:      %s  (purgeable disk cache)",
           cache_s);
  snprintf(vml5, sizeof(vml5),
           "Free Pages:        %s  (unallocated system RAM)", free_s);
  snprintf(vml6, sizeof(vml6),
           "Kernel API:        host_statistics64(HOST_VM_INFO64)");

  grid_print(left_w + 2, 2, right_w - 4, vml1, t->accent, true, false);
  grid_print(left_w + 2, 3, right_w - 4, vml2, t->dim, false, false);
  grid_print(left_w + 2, 4, right_w - 4, vml3, t->warn, false, false);
  grid_print(left_w + 2, 5, right_w - 4, vml4, t->dim, false, false);
  grid_print(left_w + 2, 6, right_w - 4, vml5, t->text, true, false);
  grid_print(left_w + 2, 7, right_w - 4, vml6, t->text, false, true);

  // 3. Full-Width Memory Usage Graph & Process Table
  int avail_h = (H - 1) - (1 + top_h);
  if (avail_h < 6)
    avail_h = 6;
  bool show_process_table = state->show_procs && (avail_h >= 14);
  int graph_h = show_process_table ? (avail_h * 52 / 100) : avail_h;
  if (graph_h < 7)
    graph_h = 7;
  if (show_process_table && (avail_h - graph_h < 6)) {
    graph_h = avail_h - 6;
  }

  grid_ttyplot(0, 1 + top_h, W, graph_h, "UNIFIED MEMORY USAGE HISTORY",
               "Mach VM Statistics", telem->history_mem,
               telem->history_mem_count, 100.0f, "%", t->mem_color, t);

  if (show_process_table) {
    int py = 1 + top_h + graph_h;
    int ph = (H - 1) - py;
    draw_process_table(state, telem, t, 0, py, W, ph,
                       "ACTIVITY MONITOR • MEMORY CONSUMERS",
                       "Sorted by %MEM (descending)");
  }

  draw_footer_bar(state, t, W, H, show_process_table);
}

// -------------------------------------------------------------
// Layout 8: NETWORK & DISK I/O VIEW
// -------------------------------------------------------------
static void render_net_view(UIState *state, const SystemTelemetry *telem,
                            const Theme *t, int W, int H) {
  draw_header_bar(telem, t, W);

  int top_h = 9;
  int left_w = W / 2;
  int right_w = W - left_w;

  // 1. Network Bandwidth Card
  char d_spd[32], u_spd[32], in_tot[32], out_tot[32];
  format_speed(telem->net_download_bps, d_spd, sizeof(d_spd));
  format_speed(telem->net_upload_bps, u_spd, sizeof(u_spd));
  format_bytes(telem->net_total_in_bytes, in_tot, sizeof(in_tot));
  format_bytes(telem->net_total_out_bytes, out_tot, sizeof(out_tot));

  grid_box(0, 1, left_w, top_h, "NETWORK BANDWIDTH & TRAFFIC", NULL, t->border,
           t->accent, t->title);

  char nl1[128], nl2[128], nl3[128], nl4[128], nl5[128], nl6[128];
  snprintf(nl1, sizeof(nl1), "Download Speed:  ▼ %s", d_spd);
  snprintf(nl2, sizeof(nl2), "Upload Speed:    ▲ %s", u_spd);
  snprintf(nl3, sizeof(nl3), "Total Inbound:   %s", in_tot);
  snprintf(nl4, sizeof(nl4), "Total Outbound:  %s", out_tot);
  snprintf(nl5, sizeof(nl5),
           "Network Engine:  Darwin BSD getifaddrs telemetry");
  snprintf(nl6, sizeof(nl6), "Socket Links:    Full-Duplex TCP/IP, UDP, QUIC");

  grid_print(2, 2, left_w - 4, nl1, t->accent, true, false);
  grid_print(2, 3, left_w - 4, nl2, t->warn, true, false);
  grid_print(2, 4, left_w - 4, nl3, t->dim, false, false);
  grid_print(2, 5, left_w - 4, nl4, t->dim, false, false);
  grid_print(2, 6, left_w - 4, nl5, t->text, false, true);
  grid_print(2, 7, left_w - 4, nl6, t->dim, false, false);

  // 2. Storage & Disk I/O Card
  char r_s[32], w_s[32], r_tot[32], w_tot[32], d_u[32], d_t[32], d_f[32];
  format_speed(telem->disk_read_bps, r_s, sizeof(r_s));
  format_speed(telem->disk_write_bps, w_s, sizeof(w_s));
  format_bytes(telem->disk_total_read_bytes, r_tot, sizeof(r_tot));
  format_bytes(telem->disk_total_write_bytes, w_tot, sizeof(w_tot));
  format_bytes(telem->disk_used_bytes, d_u, sizeof(d_u));
  format_bytes(telem->disk_total_bytes, d_t, sizeof(d_t));
  format_bytes(telem->disk_free_bytes, d_f, sizeof(d_f));

  grid_box(left_w, 1, right_w, top_h, "STORAGE DISK I/O & VOLUMES", NULL,
           t->border, t->title, t->dim);

  char dl1[128], dl2[128], dl3[128], dl4[128], dl5[128];
  snprintf(dl1, sizeof(dl1), "Disk Read Rate:  R %s", r_s);
  snprintf(dl2, sizeof(dl2), "Disk Write Rate: W %s", w_s);
  snprintf(dl3, sizeof(dl3), "Total Read:      %s  Written: %s", r_tot, w_tot);
  float disk_pct = telem->disk_total_bytes > 0
                       ? ((float)telem->disk_used_bytes /
                          (float)telem->disk_total_bytes * 100.0f)
                       : 0.0f;
  snprintf(dl4, sizeof(dl4), "Root Volume (/): Used: %s / %s (%.0f%%)", d_u,
           d_t, disk_pct);
  snprintf(dl5, sizeof(dl5), "Available Free:  %s", d_f);

  grid_print(left_w + 2, 2, right_w - 4, dl1, t->title, true, false);
  grid_print(left_w + 2, 3, right_w - 4, dl2, t->accent2, true, false);
  grid_print(left_w + 2, 4, right_w - 4, dl3, t->dim, false, false);
  grid_print(left_w + 2, 5, right_w - 4, dl4, t->text, true, false);
  grid_bar(left_w + 2, 6, right_w - 10, disk_pct, t->accent, t->dim, t->text);
  grid_print(left_w + 2, 7, right_w - 4, dl5, t->dim, false, false);

  // 3. Full-Width Network Dual-Series Graph & Process Table
  int avail_h = (H - 1) - (1 + top_h);
  if (avail_h < 6)
    avail_h = 6;
  bool show_process_table = state->show_procs && (avail_h >= 14);
  int graph_h = show_process_table ? (avail_h * 52 / 100) : avail_h;
  if (graph_h < 7)
    graph_h = 7;
  if (show_process_table && (avail_h - graph_h < 6)) {
    graph_h = avail_h - 6;
  }

  grid_ttyplot_dual(0, 1 + top_h, W, graph_h,
                    "NETWORK BANDWIDTH THROUGHPUT HISTORY", "▼ Down • ▲ Up",
                    telem->history_net_down, telem->history_net_count, "Down",
                    t->accent, telem->history_net_up, telem->history_net_count,
                    "Up", t->warn, 0.0f, "KB/s", t);

  if (show_process_table) {
    int py = 1 + top_h + graph_h;
    int ph = (H - 1) - py;
    draw_process_table(state, telem, t, 0, py, W, ph,
                       "ACTIVITY MONITOR • NETWORK & DISK PROCESSES",
                       "Network sockets & disk I/O activity");
  }

  draw_footer_bar(state, t, W, H, show_process_table);
}

// -------------------------------------------------------------
// Main Render Pass
// -------------------------------------------------------------
void ui_render(UIState *state, const SystemTelemetry *telem, const Theme *t) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 &&
      ws.ws_row > 0) {
    state->term_cols = ws.ws_col;
    state->term_rows = ws.ws_row;
  } else {
    state->term_cols = 120;
    state->term_rows = 35;
  }

  int W = state->term_cols;
  int H = state->term_rows;
  grid_reset(W, H, t);

  switch (state->view_mode) {
  case VIEW_FULL:
    render_full_view(state, telem, t, W, H);
    break;
  case VIEW_COMPACT:
    render_compact_view(state, telem, t, W, H);
    break;
  case VIEW_ZEN:
    render_zen_view(state, telem, t, W, H);
    break;
  case VIEW_OVERVIEW:
    render_overview_view(state, telem, t, W, H);
    break;
  case VIEW_CPU:
    render_cpu_view(state, telem, t, W, H);
    break;
  case VIEW_GPU:
    render_gpu_view(state, telem, t, W, H);
    break;
  case VIEW_MEM:
    render_mem_view(state, telem, t, W, H);
    break;
  case VIEW_NET:
    render_net_view(state, telem, t, W, H);
    break;
  default:
    render_full_view(state, telem, t, W, H);
    break;
  }

  // ==========================================
  // MODALS (THEME PICKER / HELP / KILL)
  // ==========================================
  if (state->modal == MODAL_THEME) {
    int m_w = 66;
    int m_h = 18;
    int m_x = (W - m_w) / 2;
    int m_y = (H - m_h) / 2;
    if (m_x < 1)
      m_x = 1;
    if (m_y < 1)
      m_y = 1;

    grid_box(m_x, m_y, m_w, m_h, "THEME CUSTOMIZER",
             "▲/▼ Browse, Enter Select, Esc Close", t->accent, t->title,
             t->dim);

    int t_cnt = theme_get_count();
    int visible_themes = m_h - 4;
    for (int i = 0; i < visible_themes && i < t_cnt; i++) {
      const Theme *th = theme_get(i);
      bool is_active = (i == theme_current_index());
      bool is_cur = (i == state->theme_cursor);
      int row_y = m_y + 2 + i;

      if (is_cur) {
        for (int c = m_x + 1; c < m_x + m_w - 1; c++) {
          grid_set(c, row_y, " ", t->proc_sel_fg, t->proc_sel_bg, true, false,
                   false);
        }
      }

      char item_buf[128];
      snprintf(item_buf, sizeof(item_buf), "%s %-18s %s%s", is_cur ? "▶" : " ",
               th->name, is_active ? "[ACTIVE] " : "", th->desc);

      if (is_cur) {
        grid_print_bg(m_x + 2, row_y, m_w - 4, item_buf, t->proc_sel_fg,
                      t->proc_sel_bg, true);
      } else {
        grid_print(m_x + 2, row_y, m_w - 4, item_buf,
                   is_active ? t->accent : t->text, is_active, false);
      }
    }
  } else if (state->modal == MODAL_HELP) {
    int m_w = 70;
    int m_h = 23;
    int m_x = (W - m_w) / 2;
    int m_y = (H - m_h) / 2;

    grid_box(m_x, m_y, m_w, m_h, "MACPULSE HELP & SHORTCUTS",
             "Press Esc or ? to Close", t->accent, t->title, t->dim);

    const char *help_lines[] = {
        "NAVIGATION & CONTROLS:",
        "  q, Ctrl+C       Quit MacPulse",
        "  t               Open Theme Customizer Modal",
        "  T (Shift+T)     Instantly cycle to next theme",
        "  v               Cycle Layout (Full, Compact, Zen, Overview, CPU, GPU, Mem, Net)",
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
        "  h, ?            Toggle this Help dialog"};
    for (int i = 0; i < 21; i++) {
      grid_print(m_x + 3, m_y + 1 + i, m_w - 6, help_lines[i],
                 (i == 0 || i == 10 || i == 17) ? t->accent : t->text,
                 (i == 0 || i == 10 || i == 17), false);
    }
  } else if (state->modal == MODAL_KILL_CONFIRM) {
    int m_w = 54;
    int m_h = 7;
    int m_x = (W - m_w) / 2;
    int m_y = (H - m_h) / 2;

    grid_box(m_x, m_y, m_w, m_h, "TERMINATE PROCESS", "Confirm", t->crit,
             t->title, t->dim);

    char kill_msg[128];
    snprintf(kill_msg, sizeof(kill_msg), "Send SIGTERM to PID %d (%s)?",
             state->target_kill_pid, state->target_kill_name);
    grid_print(m_x + 4, m_y + 2, m_w - 8, kill_msg, t->text, true, false);
    grid_print(m_x + 4, m_y + 4, m_w - 8,
               "Press [y] to Kill, or [n / Esc] to Cancel", t->accent, true,
               false);
  }

  // Flush whole 2D grid in single atomic write call
  grid_flush();
}
