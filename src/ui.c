#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <time.h>
#include <math.h>

// Dynamic String Buffer for zero-flicker single write
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

static StrBuf s_buf = {NULL, 0, 0};

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
        size_t new_cap = (b->cap * 2 > b->len + add_len + 1024) ? b->cap * 2 : (b->len + add_len + 1024);
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
    buf_init(&s_buf, 65536);
    // Enter alternate screen, hide cursor, clear
    write(STDOUT_FILENO, "\033[?1049h\033[2J\033[H\033[?25l", 18);
}

void ui_cleanup(void) {
    // Leave alternate screen, show cursor
    write(STDOUT_FILENO, "\033[?1049l\033[?25h", 12);
    buf_free(&s_buf);
}

void ui_handle_resize(void) {
    // Resize is detected via ioctl in render
}

void ui_set_status(UIState *state, const char *msg) {
    if (msg) {
        strncpy(state->status_msg, msg, sizeof(state->status_msg) - 1);
        state->status_msg_time = time(NULL);
    } else {
        state->status_msg[0] = '\0';
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
        snprintf(out, max, "%llu %s", bytes, units[u]);
    } else {
        snprintf(out, max, "%.2f %s", d, units[u]);
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
        snprintf(out, max, "%3.0f %s", d, units[u]);
    } else {
        snprintf(out, max, "%5.1f %s", d, units[u]);
    }
}

// Draw a filled colored horizontal progress bar
static void draw_bar(StrBuf *b, int width, float pct, ColorRGB fill_color, ColorRGB bg_color) {
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;

    int filled = (int)((pct / 100.0f) * width);
    if (filled > width) filled = width;

    char fg_str[32], bg_str[32];
    ansi_fg_rgb(fg_str, sizeof(fg_str), fill_color);
    ansi_fg_rgb(bg_str, sizeof(bg_str), bg_color);

    buf_append_str(b, fg_str);
    for (int i = 0; i < filled; i++) {
        buf_append_str(b, "█");
    }
    buf_append_str(b, bg_str);
    for (int i = filled; i < width; i++) {
        buf_append_str(b, "░");
    }
    buf_append_str(b, "\033[0m");
}

// Draw a multi-row sparkline / block graph
static void draw_graph(StrBuf *b, int x, int y, int width, int height,
                       const float *history, int count, float max_val,
                       ColorRGB low_color, ColorRGB high_color, ColorRGB dim_color) {
    const char *blocks[] = {" ", " ", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
    if (max_val <= 0.0f) max_val = 100.0f;

    for (int r = 0; r < height; r++) {
        buf_printf(b, "\033[%d;%dH", y + r, x);
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
                // Empty cell
                char dim_str[32];
                ansi_fg_rgb(dim_str, sizeof(dim_str), dim_color);
                buf_printf(b, "%s·\033[0m", dim_str);
            } else if (val >= row_top) {
                // Full cell
                float t = val / max_val;
                ColorRGB c_col = color_lerp(low_color, high_color, t);
                char fg_str[32];
                ansi_fg_rgb(fg_str, sizeof(fg_str), c_col);
                buf_printf(b, "%s█\033[0m", fg_str);
            } else {
                // Partial block
                float fraction = (val - row_bottom) / row_height;
                int block_idx = (int)(fraction * 8.0f);
                if (block_idx < 1) block_idx = 1;
                if (block_idx > 8) block_idx = 8;

                float t = val / max_val;
                ColorRGB c_col = color_lerp(low_color, high_color, t);
                char fg_str[32];
                ansi_fg_rgb(fg_str, sizeof(fg_str), c_col);
                buf_printf(b, "%s%s\033[0m", fg_str, blocks[block_idx]);
            }
        }
    }
}

// Draw a framed box with rounded corners and a title
static void draw_box(StrBuf *b, int x, int y, int width, int height,
                     const char *title, const char *extra, const Theme *t) {
    char border_str[32], title_str[32], extra_str[32];
    ansi_fg_rgb(border_str, sizeof(border_str), t->border);
    ansi_fg_rgb(title_str, sizeof(title_str), t->title);
    ansi_fg_rgb(extra_str, sizeof(extra_str), t->accent2);

    // Top border
    buf_printf(b, "\033[%d;%dH%s╭─ ", y, x, border_str);
    int cur_len = 3;
    if (title && title[0]) {
        buf_printf(b, "\033[1m%s%s\033[0m%s ", title_str, title, border_str);
        cur_len += strlen(title) + 1;
    }
    if (extra && extra[0]) {
        buf_printf(b, "%s%s%s ", extra_str, extra, border_str);
        cur_len += strlen(extra) + 1;
    }

    int remain = width - cur_len - 1;
    for (int i = 0; i < remain; i++) buf_append_str(b, "─");
    buf_printf(b, "╮\033[0m");

    // Left and Right walls
    for (int r = 1; r < height - 1; r++) {
        buf_printf(b, "\033[%d;%dH%s│\033[0m", y + r, x, border_str);
        buf_printf(b, "\033[%d;%dH%s│\033[0m", y + r, x + width - 1, border_str);
    }

    // Bottom border
    buf_printf(b, "\033[%d;%dH%s╰", y + height - 1, x, border_str);
    for (int i = 0; i < width - 2; i++) buf_append_str(b, "─");
    buf_printf(b, "╯\033[0m");
}

void ui_render(UIState *state, const SystemTelemetry *telem, const Theme *t) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        state->term_cols = ws.ws_col;
        state->term_rows = ws.ws_row;
    } else {
        state->term_cols = 120;
        state->term_rows = 40;
    }

    int W = state->term_cols;
    int H = state->term_rows;

    buf_reset(&s_buf);
    // Home cursor and clear screen
    buf_append_str(&s_buf, "\033[H");

    // Colors
    char text_str[32], dim_str[32], accent_str[32], warn_str[32], crit_str[32];
    char header_bg[32], header_fg[32], cpu_str[32], gpu_str[32], mem_str[32], net_d_str[32], net_u_str[32];
    ansi_fg_rgb(text_str, sizeof(text_str), t->text);
    ansi_fg_rgb(dim_str, sizeof(dim_str), t->dim);
    ansi_fg_rgb(accent_str, sizeof(accent_str), t->accent);
    ansi_fg_rgb(warn_str, sizeof(warn_str), t->warn);
    ansi_fg_rgb(crit_str, sizeof(crit_str), t->crit);
    ansi_bg_rgb(header_bg, sizeof(header_bg), t->header_bg);
    ansi_fg_rgb(header_fg, sizeof(header_fg), t->header_fg);
    ansi_fg_rgb(cpu_str, sizeof(cpu_str), t->cpu_color);
    ansi_fg_rgb(gpu_str, sizeof(gpu_str), t->gpu_color);
    ansi_fg_rgb(mem_str, sizeof(mem_str), t->mem_color);
    ansi_fg_rgb(net_d_str, sizeof(net_d_str), t->net_down);
    ansi_fg_rgb(net_u_str, sizeof(net_u_str), t->net_up);

    // ==========================================
    // 1. TOP HEADER BAR
    // ==========================================
    buf_printf(&s_buf, "\033[1;1H%s%s", header_bg, header_fg);
    char hdr_left[256];
    snprintf(hdr_left, sizeof(hdr_left), " ◈ MacPulse v1.0.0 │ %s (%d Cores: %dP + %dE) │ %s ",
             telem->chip_name, telem->total_cores, telem->p_cores, telem->e_cores, telem->model_name);

    time_t raw_time = time(NULL);
    struct tm *time_info = localtime(&raw_time);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", time_info);

    char hdr_right[256];
    if (telem->has_battery) {
        snprintf(hdr_right, sizeof(hdr_right), " ⚡ %d%%%s │ %s │ Up: %s │ %s ",
                 telem->battery_pct, telem->battery_charging ? " [CHG]" : "",
                 telem->thermal_state_str, telem->uptime_str, time_str);
    } else {
        snprintf(hdr_right, sizeof(hdr_right), " 🔌 AC │ %s │ Up: %s │ %s ",
                 telem->thermal_state_str, telem->uptime_str, time_str);
    }

    int pad = W - (int)strlen(hdr_left) - (int)strlen(hdr_right);
    buf_append_str(&s_buf, hdr_left);
    for (int i = 0; i < (pad > 0 ? pad : 1); i++) buf_append_str(&s_buf, " ");
    buf_append_str(&s_buf, hdr_right);
    buf_append_str(&s_buf, "\033[0m");

    // ==========================================
    // 2. GRID DIMENSIONS
    // ==========================================
    int half_w = W / 2;
    int right_w = W - half_w;

    int top_y = 2;
    int top_h = 10;
    int mid_y = top_y + top_h;
    int mid_h = 8;
    int bot_y = mid_y + mid_h;
    int bot_h = H - bot_y; // Remaining height for Activity Monitor & footer

    if (H < 32) {
        top_h = 8;
        mid_h = 6;
        mid_y = top_y + top_h;
        bot_y = mid_y + mid_h;
        bot_h = H - bot_y;
    }

    // ==========================================
    // 3. ROW 1 (LEFT): CPU MONITOR & GRAPH
    // ==========================================
    char cpu_extra[64];
    snprintf(cpu_extra, sizeof(cpu_extra), "%.1f°C (Peak: %.1f°C)", telem->cpu_temp_avg, telem->cpu_temp_max);
    draw_box(&s_buf, 1, top_y, half_w, top_h, "CPU MONITOR", cpu_extra, t);

    // CPU Total Gauge
    int gauge_w = half_w - 28;
    if (gauge_w < 8) gauge_w = 8;
    buf_printf(&s_buf, "\033[%d;3H%sTotal: %s", top_y + 1, dim_str, text_str);
    draw_bar(&s_buf, gauge_w, telem->cpu_total_pct, t->cpu_color, t->dim);
    buf_printf(&s_buf, " \033[1m%s%5.1f%%\033[0m", cpu_str, telem->cpu_total_pct);

    // Detailed User / Sys / Idle
    buf_printf(&s_buf, "\033[%d;3H%sUser: %s%4.1f%% %sSys: %s%4.1f%% %sIdle: %s%4.1f%% %sLoad: %s%.2f %.2f %.2f",
               top_y + 2,
               dim_str, text_str, telem->cpu_user_pct,
               dim_str, text_str, telem->cpu_sys_pct,
               dim_str, text_str, telem->cpu_idle_pct,
               dim_str, text_str, telem->load_avg[0], telem->load_avg[1], telem->load_avg[2]);

    // Per-core mini summary
    buf_printf(&s_buf, "\033[%d;3H%sP-Cores: ", top_y + 3, accent_str);
    int p_draw = telem->p_cores < 6 ? telem->p_cores : 6;
    for (int i = 0; i < p_draw; i++) {
        buf_printf(&s_buf, "%s%d:%s%2.0f%% ", dim_str, i, text_str, telem->core_pct[i]);
    }

    buf_printf(&s_buf, "\033[%d;3H%sE-Cores: ", top_y + 4, accent_str);
    int e_draw = telem->e_cores < 8 ? telem->e_cores : 8;
    for (int i = 0; i < e_draw; i++) {
        int core_idx = telem->p_cores + i;
        buf_printf(&s_buf, "%s%d:%s%2.0f%% ", dim_str, i, text_str, telem->core_pct[core_idx]);
    }

    // Sparkline Graph of CPU Load
    int graph_y = top_y + 5;
    int graph_h = top_h - 6;
    if (graph_h > 0) {
        int g_width = half_w - 4;
        draw_graph(&s_buf, 3, graph_y, g_width, graph_h,
                   telem->history_cpu, telem->history_cpu_count, 100.0f,
                   t->cpu_color, t->crit, t->dim);
    }

    // ==========================================
    // 4. ROW 1 (RIGHT): GPU & THERMAL MONITOR
    // ==========================================
    char gpu_extra[64];
    snprintf(gpu_extra, sizeof(gpu_extra), "%.1f°C (SoC: %.1f°C)", telem->gpu_temp_avg, telem->soc_temp_max);
    draw_box(&s_buf, half_w + 1, top_y, right_w, top_h, "GPU & THERMAL", gpu_extra, t);

    // GPU Total Gauge
    int gpu_gauge_w = right_w - 28;
    if (gpu_gauge_w < 8) gpu_gauge_w = 8;
    buf_printf(&s_buf, "\033[%d;%dH%sDevice: %s", top_y + 1, half_w + 3, dim_str, text_str);
    draw_bar(&s_buf, gpu_gauge_w, telem->gpu_device_pct, t->gpu_color, t->dim);
    buf_printf(&s_buf, " \033[1m%s%5.1f%%\033[0m", gpu_str, telem->gpu_device_pct);

    // Renderer & Tiler & VRAM
    char vram_in_use[32], vram_alloc[32];
    format_bytes(telem->gpu_in_use_bytes, vram_in_use, sizeof(vram_in_use));
    format_bytes(telem->gpu_alloc_bytes, vram_alloc, sizeof(vram_alloc));

    buf_printf(&s_buf, "\033[%d;%dH%sRenderer: %s%4.1f%% %sTiler: %s%4.1f%% %sVRAM: %s%s / %s",
               top_y + 2, half_w + 3,
               dim_str, text_str, telem->gpu_renderer_pct,
               dim_str, text_str, telem->gpu_tiler_pct,
               dim_str, text_str, vram_in_use, vram_alloc);

    // Thermal breakdown
    buf_printf(&s_buf, "\033[%d;%dH%sThermal: %s[%s] %sSSD: %s%.1f°C %sBattery: %s%.1f°C %sSoC Peak: %s%.1f°C",
               top_y + 3, half_w + 3,
               dim_str, accent_str, telem->thermal_state_str,
               dim_str, text_str, telem->ssd_temp,
               dim_str, text_str, telem->battery_temp,
               dim_str, crit_str, telem->soc_temp_max);

    // GPU History Graph
    if (graph_h > 0) {
        int g_width = right_w - 4;
        draw_graph(&s_buf, half_w + 3, graph_y, g_width, graph_h,
                   telem->history_gpu, telem->history_gpu_count, 100.0f,
                   t->gpu_color, t->crit, t->dim);
    }

    // ==========================================
    // 5. ROW 2 (LEFT): UNIFIED MEMORY
    // ==========================================
    char mem_extra[64];
    char total_ram_str[32], used_ram_str[32];
    format_bytes(telem->mem_total_bytes, total_ram_str, sizeof(total_ram_str));
    format_bytes(telem->mem_used_bytes, used_ram_str, sizeof(used_ram_str));
    snprintf(mem_extra, sizeof(mem_extra), "%s / %s (%.1f%%)", used_ram_str, total_ram_str, telem->mem_used_pct);
    draw_box(&s_buf, 1, mid_y, half_w, mid_h, "UNIFIED MEMORY", mem_extra, t);

    // Segmented bar
    buf_printf(&s_buf, "\033[%d;3H%sRAM:  %s", mid_y + 1, dim_str, text_str);
    int mem_bar_w = half_w - 26;
    if (mem_bar_w < 8) mem_bar_w = 8;
    draw_bar(&s_buf, mem_bar_w, telem->mem_used_pct, t->mem_color, t->dim);
    buf_printf(&s_buf, " \033[1m%s%5.1f%%\033[0m", mem_str, telem->mem_used_pct);

    // Memory Breakdown details
    char app_str[32], wired_str[32], comp_str[32], cache_str[32], free_str[32], swap_str[32];
    format_bytes(telem->mem_active_bytes, app_str, sizeof(app_str));
    format_bytes(telem->mem_wired_bytes, wired_str, sizeof(wired_str));
    format_bytes(telem->mem_compressed_bytes, comp_str, sizeof(comp_str));
    format_bytes(telem->mem_inactive_bytes, cache_str, sizeof(cache_str));
    format_bytes(telem->mem_free_bytes, free_str, sizeof(free_str));
    format_bytes(telem->swap_used_bytes, swap_str, sizeof(swap_str));

    buf_printf(&s_buf, "\033[%d;3H%sApp Memory:   %s%-9s %sWired Memory: %s%-9s %sCompressed: %s%s",
               mid_y + 2, dim_str, text_str, app_str, dim_str, text_str, wired_str, dim_str, text_str, comp_str);
    buf_printf(&s_buf, "\033[%d;3H%sCached Files: %s%-9s %sFree Memory:  %s%-9s %sSwap Used:  %s%s",
               mid_y + 3, dim_str, text_str, cache_str, dim_str, text_str, free_str, dim_str, text_str, swap_str);

    // Visual breakdown labels
    buf_printf(&s_buf, "\033[%d;3H%sBreakdown: %s[App: %s] %s[Wired: %s] %s[Comp: %s] %s[Cache: %s]",
               mid_y + 4, dim_str,
               accent_str, app_str,
               gpu_str, wired_str,
               warn_str, comp_str,
               dim_str, cache_str);

    // ==========================================
    // 6. ROW 2 (RIGHT): NETWORK SPEED & TRAFFIC
    // ==========================================
    char net_down_str[32], net_up_str[32];
    format_speed(telem->net_download_bps, net_down_str, sizeof(net_down_str));
    format_speed(telem->net_upload_bps, net_up_str, sizeof(net_up_str));

    char net_extra[64];
    snprintf(net_extra, sizeof(net_extra), "▼ %s  ▲ %s", net_down_str, net_up_str);
    draw_box(&s_buf, half_w + 1, mid_y, right_w, mid_h, "NETWORK SPEED", net_extra, t);

    char tot_in[32], tot_out[32];
    format_bytes(telem->net_total_in_bytes, tot_in, sizeof(tot_in));
    format_bytes(telem->net_total_out_bytes, tot_out, sizeof(tot_out));

    buf_printf(&s_buf, "\033[%d;%dH%sDownload: \033[1m%s▼ %-10s\033[0m %sUpload: \033[1m%s▲ %-10s\033[0m %sTotal In: %s%s",
               mid_y + 1, half_w + 3,
               dim_str, net_d_str, net_down_str,
               dim_str, net_u_str, net_up_str,
               dim_str, text_str, tot_in);

    buf_printf(&s_buf, "\033[%d;%dH%sTotal Out: %s%-10s %sBandwidth Rolling Graph (KB/s):",
               mid_y + 2, half_w + 3,
               dim_str, text_str, tot_out, dim_str);

    // Dual Graph of Network Speed
    int net_graph_h = mid_h - 4;
    if (net_graph_h > 0) {
        int g_width = right_w - 4;
        // Find max speed in history
        float max_kb = 100.0f;
        for (int i = 0; i < telem->history_net_count; i++) {
            if (telem->history_net_down[i] > max_kb) max_kb = telem->history_net_down[i];
            if (telem->history_net_up[i] > max_kb) max_kb = telem->history_net_up[i];
        }
        draw_graph(&s_buf, half_w + 3, mid_y + 3, g_width, net_graph_h,
                   telem->history_net_down, telem->history_net_count, max_kb,
                   t->net_down, t->net_up, t->dim);
    }

    // ==========================================
    // 7. ROW 3: ACTIVITY MONITOR (PROCESS TABLE)
    // ==========================================
    char act_extra[64];
    if (state->search_buf[0]) {
        snprintf(act_extra, sizeof(act_extra), "%d/%d procs (filter: \"%s\")",
                 telem->procs.filtered_count, telem->procs.count, state->search_buf);
    } else {
        snprintf(act_extra, sizeof(act_extra), "%d processes", telem->procs.count);
    }
    draw_box(&s_buf, 1, bot_y, W, bot_h - 1, "ACTIVITY MONITOR", act_extra, t);

    // Table Header
    buf_printf(&s_buf, "\033[%d;3H\033[1;4m%s%-7s %-14s %-8s %-8s %-12s %-8s %s\033[0m",
               bot_y + 1, accent_str,
               "PID", "USER", "%CPU", "%MEM", "RESIDENT", "STATE", "COMMAND");

    int max_rows = bot_h - 4;
    if (max_rows < 1) max_rows = 1;

    // Adjust scroll bounds
    if (state->proc_selected < 0) state->proc_selected = 0;
    if (state->proc_selected >= telem->procs.filtered_count) {
        state->proc_selected = telem->procs.filtered_count > 0 ? telem->procs.filtered_count - 1 : 0;
    }
    if (state->proc_selected < state->proc_scroll) {
        state->proc_scroll = state->proc_selected;
    }
    if (state->proc_selected >= state->proc_scroll + max_rows) {
        state->proc_scroll = state->proc_selected - max_rows + 1;
    }

    char sel_bg_str[32], sel_fg_str[32];
    ansi_bg_rgb(sel_bg_str, sizeof(sel_bg_str), t->proc_sel_bg);
    ansi_fg_rgb(sel_fg_str, sizeof(sel_fg_str), t->proc_sel_fg);

    for (int r = 0; r < max_rows; r++) {
        int idx = state->proc_scroll + r;
        int row_y = bot_y + 2 + r;
        buf_printf(&s_buf, "\033[%d;3H", row_y);

        if (idx < telem->procs.filtered_count) {
            int p_idx = telem->procs.filtered_indices[idx];
            const ProcessEntry *pe = &telem->procs.items[p_idx];

            char rss_str[32];
            format_bytes(pe->mem_bytes, rss_str, sizeof(rss_str));

            bool is_sel = (idx == state->proc_selected);

            if (is_sel) {
                buf_printf(&s_buf, "%s%s", sel_bg_str, sel_fg_str);
            } else {
                buf_printf(&s_buf, "%s", text_str);
            }

            // Command name truncated to fill width
            int cmd_w = W - 65;
            if (cmd_w < 10) cmd_w = 10;
            char cmd_trunc[128];
            strncpy(cmd_trunc, pe->name, sizeof(cmd_trunc) - 1);
            if ((int)strlen(cmd_trunc) > cmd_w) cmd_trunc[cmd_w] = '\0';

            buf_printf(&s_buf, "%-7d %-14.14s %6.1f%%  %6.1f%%  %-12s %-8s %-*s\033[0m",
                       pe->pid, pe->username, pe->cpu_pct, pe->mem_pct, rss_str, pe->state, cmd_w, cmd_trunc);
        } else {
            // Empty row: clear to border
            buf_printf(&s_buf, "%s", dim_str);
            for (int c = 0; c < W - 4; c++) buf_append_str(&s_buf, " ");
            buf_append_str(&s_buf, "\033[0m");
        }
    }

    // ==========================================
    // 8. BOTTOM FOOTER / SHORTCUT BAR
    // ==========================================
    buf_printf(&s_buf, "\033[%d;1H%s%s", H, header_bg, header_fg);
    char ftr[512];
    if (state->searching) {
        snprintf(ftr, sizeof(ftr), " Search: %s█  (Press Enter to finish, Esc to cancel)", state->search_buf);
    } else if (time(NULL) - state->status_msg_time < 3 && state->status_msg[0]) {
        snprintf(ftr, sizeof(ftr), " %s ", state->status_msg);
    } else {
        snprintf(ftr, sizeof(ftr), " [q]Quit [t]Theme [c]CPU [m]Mem [p]PID [n]Name [/]Filter [x]Kill [h]Help [+/-]%.1fs",
                 (float)state->refresh_ms / 1000.0f);
    }
    buf_append_str(&s_buf, ftr);
    int ftr_len = (int)strlen(ftr);
    for (int i = 0; i < W - ftr_len; i++) buf_append_str(&s_buf, " ");
    buf_append_str(&s_buf, "\033[0m");

    // ==========================================
    // 9. MODALS
    // ==========================================
    if (state->modal == MODAL_THEME) {
        int m_w = 64;
        int m_h = 18;
        int m_x = (W - m_w) / 2;
        int m_y = (H - m_h) / 2;
        if (m_x < 1) m_x = 1;
        if (m_y < 1) m_y = 1;

        draw_box(&s_buf, m_x, m_y, m_w, m_h, "THEME CUSTOMIZER", "▲/▼ Browse, Enter Select, Esc Close", t);

        int t_cnt = theme_get_count();
        int visible_themes = m_h - 4;
        for (int i = 0; i < visible_themes && i < t_cnt; i++) {
            const Theme *th = theme_get(i);
            bool is_active = (i == theme_current_index());
            bool is_cur = (i == state->theme_cursor);

            buf_printf(&s_buf, "\033[%d;%dH", m_y + 2 + i, m_x + 3);
            if (is_cur) {
                buf_printf(&s_buf, "%s%s ▶ ", sel_bg_str, sel_fg_str);
            } else {
                buf_printf(&s_buf, "   ");
            }

            // Swatch of colors
            char c1[32], c2[32], c3[32], c4[32];
            ansi_fg_rgb(c1, sizeof(c1), th->accent);
            ansi_fg_rgb(c2, sizeof(c2), th->cpu_color);
            ansi_fg_rgb(c3, sizeof(c3), th->gpu_color);
            ansi_fg_rgb(c4, sizeof(c4), th->title);

            buf_printf(&s_buf, "%s█%s█%s█%s█\033[0m ", c1, c2, c3, c4);
            buf_printf(&s_buf, "\033[1m%-18s\033[0m %s%s%s\033[0m",
                       th->name,
                       is_active ? accent_str : dim_str,
                       is_active ? "[ACTIVE] " : "",
                       th->desc);
            buf_append_str(&s_buf, "\033[0m");
        }
    } else if (state->modal == MODAL_HELP) {
        int m_w = 66;
        int m_h = 19;
        int m_x = (W - m_w) / 2;
        int m_y = (H - m_h) / 2;

        draw_box(&s_buf, m_x, m_y, m_w, m_h, "MACPULSE HELP & SHORTCUTS", "Press Esc or ? to Close", t);

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
            "  + / -           Speed up / slow down refresh rate (0.5s - 3s)",
            "  h, ?            Toggle this Help dialog"
        };
        for (int i = 0; i < 17; i++) {
            buf_printf(&s_buf, "\033[%d;%dH%s%s\033[0m", m_y + 1 + i, m_x + 3,
                       (i == 0 || i == 8 || i == 14) ? accent_str : text_str, help_lines[i]);
        }
    } else if (state->modal == MODAL_KILL_CONFIRM) {
        int m_w = 54;
        int m_h = 7;
        int m_x = (W - m_w) / 2;
        int m_y = (H - m_h) / 2;

        draw_box(&s_buf, m_x, m_y, m_w, m_h, "TERMINATE PROCESS", "Confirm", t);
        buf_printf(&s_buf, "\033[%d;%dH%sSend SIGTERM to PID \033[1m%s%d\033[0m (%s)?",
                   m_y + 2, m_x + 4, text_str, crit_str, state->target_kill_pid, state->target_kill_name);
        buf_printf(&s_buf, "\033[%d;%dH%sPress \033[1m[y]\033[0m to Kill, or \033[1m[n / Esc]\033[0m to Cancel",
                   m_y + 4, m_x + 6, accent_str);
    }

    // Single atomic write to stdout
    write(STDOUT_FILENO, s_buf.data, s_buf.len);
}
