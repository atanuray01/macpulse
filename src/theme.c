#include "theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

static Theme s_themes[MAX_THEMES];
static int s_theme_count = 0;
static int s_current_theme_index = 0;

ColorRGB color_hex(const char *hex) {
    ColorRGB c = {255, 255, 255};
    if (!hex) return c;
    if (hex[0] == '#') hex++;
    if (strlen(hex) < 6) return c;
    unsigned int r = 255, g = 255, b = 255;
    if (sscanf(hex, "%02x%02x%02x", &r, &g, &b) == 3) {
        c.r = (uint8_t)r;
        c.g = (uint8_t)g;
        c.b = (uint8_t)b;
    }
    return c;
}

ColorRGB color_lerp(ColorRGB a, ColorRGB b, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    ColorRGB r;
    r.r = (uint8_t)(a.r + (b.r - a.r) * t);
    r.g = (uint8_t)(a.g + (b.g - a.g) * t);
    r.b = (uint8_t)(a.b + (b.b - a.b) * t);
    return r;
}

void ansi_fg_rgb(char *buf, size_t max, ColorRGB c) {
    snprintf(buf, max, "\033[38;2;%u;%u;%um", c.r, c.g, c.b);
}

void ansi_bg_rgb(char *buf, size_t max, ColorRGB c) {
    snprintf(buf, max, "\033[48;2;%u;%u;%um", c.r, c.g, c.b);
}

static void add_theme(Theme t) {
    if (s_theme_count < MAX_THEMES) {
        s_themes[s_theme_count++] = t;
    }
}

void theme_init_defaults(void) {
    s_theme_count = 0;

    // 1. Cyberpunk 2077
    add_theme((Theme){
        .name = "Cyberpunk 2077",
        .desc = "High contrast neon pink, electric yellow and laser cyan",
        .border = color_hex("#ff007f"),
        .header_bg = color_hex("#260033"),
        .header_fg = color_hex("#00f0ff"),
        .title = color_hex("#ffe600"),
        .accent = color_hex("#00f0ff"),
        .accent2 = color_hex("#ff007f"),
        .cpu_color = color_hex("#00f0ff"),
        .gpu_color = color_hex("#ff007f"),
        .mem_color = color_hex("#ffe600"),
        .net_down = color_hex("#00ff66"),
        .net_up = color_hex("#ff7700"),
        .text = color_hex("#ffffff"),
        .dim = color_hex("#707090"),
        .warn = color_hex("#ffe600"),
        .crit = color_hex("#ff003c"),
        .proc_sel_bg = color_hex("#3d005c"),
        .proc_sel_fg = color_hex("#00ffff")
    });

    // 2. Tokyo Night
    add_theme((Theme){
        .name = "Tokyo Night",
        .desc = "Modern clean indigo, soft cyan, purple and warm orange",
        .border = color_hex("#7aa2f7"),
        .header_bg = color_hex("#1f2335"),
        .header_fg = color_hex("#7dcfff"),
        .title = color_hex("#bb9af7"),
        .accent = color_hex("#7dcfff"),
        .accent2 = color_hex("#ff9e64"),
        .cpu_color = color_hex("#7aa2f7"),
        .gpu_color = color_hex("#bb9af7"),
        .mem_color = color_hex("#7dcfff"),
        .net_down = color_hex("#9ece6a"),
        .net_up = color_hex("#e0af68"),
        .text = color_hex("#c0caf5"),
        .dim = color_hex("#565f89"),
        .warn = color_hex("#e0af68"),
        .crit = color_hex("#f7768e"),
        .proc_sel_bg = color_hex("#28345a"),
        .proc_sel_fg = color_hex("#c0caf5")
    });

    // 3. Dracula
    add_theme((Theme){
        .name = "Dracula Pro",
        .desc = "Iconic gothic dark theme with vibrant pastels",
        .border = color_hex("#bd93f9"),
        .header_bg = color_hex("#282a36"),
        .header_fg = color_hex("#50fa7b"),
        .title = color_hex("#ff79c6"),
        .accent = color_hex("#bd93f9"),
        .accent2 = color_hex("#8be9fd"),
        .cpu_color = color_hex("#50fa7b"),
        .gpu_color = color_hex("#ff79c6"),
        .mem_color = color_hex("#8be9fd"),
        .net_down = color_hex("#50fa7b"),
        .net_up = color_hex("#ffb86c"),
        .text = color_hex("#f8f8f2"),
        .dim = color_hex("#6272a4"),
        .warn = color_hex("#ffb86c"),
        .crit = color_hex("#ff5555"),
        .proc_sel_bg = color_hex("#44475a"),
        .proc_sel_fg = color_hex("#f8f8f2")
    });

    // 4. Nord Arctic
    add_theme((Theme){
        .name = "Nord Arctic",
        .desc = "Arctic blue, aurora green and clean glacial tones",
        .border = color_hex("#88c0d0"),
        .header_bg = color_hex("#2e3440"),
        .header_fg = color_hex("#8fbcbb"),
        .title = color_hex("#81a1c1"),
        .accent = color_hex("#88c0d0"),
        .accent2 = color_hex("#b48ead"),
        .cpu_color = color_hex("#88c0d0"),
        .gpu_color = color_hex("#b48ead"),
        .mem_color = color_hex("#81a1c1"),
        .net_down = color_hex("#a3be8c"),
        .net_up = color_hex("#ebcb8b"),
        .text = color_hex("#eceff4"),
        .dim = color_hex("#4c566a"),
        .warn = color_hex("#ebcb8b"),
        .crit = color_hex("#bf616a"),
        .proc_sel_bg = color_hex("#3b4252"),
        .proc_sel_fg = color_hex("#eceff4")
    });

    // 5. Catppuccin Mocha
    add_theme((Theme){
        .name = "Catppuccin Mocha",
        .desc = "Warm soothing pastel palette with mauve, peach and teal",
        .border = color_hex("#cba6f7"),
        .header_bg = color_hex("#1e1e2e"),
        .header_fg = color_hex("#94e2d5"),
        .title = color_hex("#fab387"),
        .accent = color_hex("#cba6f7"),
        .accent2 = color_hex("#89b4fa"),
        .cpu_color = color_hex("#89b4fa"),
        .gpu_color = color_hex("#cba6f7"),
        .mem_color = color_hex("#94e2d5"),
        .net_down = color_hex("#a6e3a1"),
        .net_up = color_hex("#fab387"),
        .text = color_hex("#cdd6f4"),
        .dim = color_hex("#585b70"),
        .warn = color_hex("#f9e2af"),
        .crit = color_hex("#f38ba8"),
        .proc_sel_bg = color_hex("#313244"),
        .proc_sel_fg = color_hex("#cdd6f4")
    });

    // 6. Monokai Pro
    add_theme((Theme){
        .name = "Monokai Pro",
        .desc = "High-energy magenta, acid yellow, and bright mint",
        .border = color_hex("#ff6188"),
        .header_bg = color_hex("#2d2a2e"),
        .header_fg = color_hex("#ffd866"),
        .title = color_hex("#a9dc76"),
        .accent = color_hex("#78dce8"),
        .accent2 = color_hex("#fc9867"),
        .cpu_color = color_hex("#78dce8"),
        .gpu_color = color_hex("#ff6188"),
        .mem_color = color_hex("#ffd866"),
        .net_down = color_hex("#a9dc76"),
        .net_up = color_hex("#fc9867"),
        .text = color_hex("#fcfcfa"),
        .dim = color_hex("#727072"),
        .warn = color_hex("#ffd866"),
        .crit = color_hex("#ff6188"),
        .proc_sel_bg = color_hex("#403e41"),
        .proc_sel_fg = color_hex("#ffd866")
    });

    // 7. Gruvbox Dark
    add_theme((Theme){
        .name = "Gruvbox Dark",
        .desc = "Warm retro groove with gold, aqua, and autumn orange",
        .border = color_hex("#fabd2f"),
        .header_bg = color_hex("#282828"),
        .header_fg = color_hex("#8ec07c"),
        .title = color_hex("#fabd2f"),
        .accent = color_hex("#fe8019"),
        .accent2 = color_hex("#b8bb26"),
        .cpu_color = color_hex("#8ec07c"),
        .gpu_color = color_hex("#d3869b"),
        .mem_color = color_hex("#fabd2f"),
        .net_down = color_hex("#b8bb26"),
        .net_up = color_hex("#fe8019"),
        .text = color_hex("#ebdbb2"),
        .dim = color_hex("#665c54"),
        .warn = color_hex("#fabd2f"),
        .crit = color_hex("#fb4934"),
        .proc_sel_bg = color_hex("#3c3836"),
        .proc_sel_fg = color_hex("#ebdbb2")
    });

    // 8. Matrix Hacker
    add_theme((Theme){
        .name = "Matrix Hacker",
        .desc = "Digital rain monochrome phosphor greens",
        .border = color_hex("#00ff66"),
        .header_bg = color_hex("#051a08"),
        .header_fg = color_hex("#55ff99"),
        .title = color_hex("#00ff66"),
        .accent = color_hex("#00ff66"),
        .accent2 = color_hex("#55ff99"),
        .cpu_color = color_hex("#00ff66"),
        .gpu_color = color_hex("#55ff99"),
        .mem_color = color_hex("#33cc66"),
        .net_down = color_hex("#00ff66"),
        .net_up = color_hex("#88ffaa"),
        .text = color_hex("#b3ffcc"),
        .dim = color_hex("#1a592e"),
        .warn = color_hex("#ffff33"),
        .crit = color_hex("#ff3333"),
        .proc_sel_bg = color_hex("#0a3314"),
        .proc_sel_fg = color_hex("#00ff66")
    });

    // 9. Solarized Dark
    add_theme((Theme){
        .name = "Solarized Dark",
        .desc = "Precision engineered scientific palette with teal and cyan",
        .border = color_hex("#2aa198"),
        .header_bg = color_hex("#073642"),
        .header_fg = color_hex("#268bd2"),
        .title = color_hex("#2aa198"),
        .accent = color_hex("#268bd2"),
        .accent2 = color_hex("#6c71c4"),
        .cpu_color = color_hex("#268bd2"),
        .gpu_color = color_hex("#6c71c4"),
        .mem_color = color_hex("#2aa198"),
        .net_down = color_hex("#859900"),
        .net_up = color_hex("#b58900"),
        .text = color_hex("#93a1a1"),
        .dim = color_hex("#586e75"),
        .warn = color_hex("#b58900"),
        .crit = color_hex("#dc322f"),
        .proc_sel_bg = color_hex("#073642"),
        .proc_sel_fg = color_hex("#eee8d5")
    });

    // 10. Apple Classic
    add_theme((Theme){
        .name = "Apple Classic",
        .desc = "Minimalist Cupertino styling with silver, blue and emerald",
        .border = color_hex("#007aff"),
        .header_bg = color_hex("#1c1c1e"),
        .header_fg = color_hex("#ffffff"),
        .title = color_hex("#007aff"),
        .accent = color_hex("#007aff"),
        .accent2 = color_hex("#5856d6"),
        .cpu_color = color_hex("#007aff"),
        .gpu_color = color_hex("#af52de"),
        .mem_color = color_hex("#34c759"),
        .net_down = color_hex("#30d158"),
        .net_up = color_hex("#ff9f0a"),
        .text = color_hex("#f2f2f7"),
        .dim = color_hex("#636366"),
        .warn = color_hex("#ff9f0a"),
        .crit = color_hex("#ff453a"),
        .proc_sel_bg = color_hex("#2c2c2e"),
        .proc_sel_fg = color_hex("#ffffff")
    });

    // 11. Sunset Horizon
    add_theme((Theme){
        .name = "Sunset Horizon",
        .desc = "Warm twilight gradient with flamingo pink and golden mango",
        .border = color_hex("#ff5e62"),
        .header_bg = color_hex("#2b1123"),
        .header_fg = color_hex("#ff9966"),
        .title = color_hex("#ff5e62"),
        .accent = color_hex("#ff9966"),
        .accent2 = color_hex("#d152a5"),
        .cpu_color = color_hex("#ff5e62"),
        .gpu_color = color_hex("#d152a5"),
        .mem_color = color_hex("#ff9966"),
        .net_down = color_hex("#ffd166"),
        .net_up = color_hex("#ef476f"),
        .text = color_hex("#ffe5ec"),
        .dim = color_hex("#7a4d65"),
        .warn = color_hex("#ffd166"),
        .crit = color_hex("#ef476f"),
        .proc_sel_bg = color_hex("#421d37"),
        .proc_sel_fg = color_hex("#ff9966")
    });

    // 12. Synthwave 84
    add_theme((Theme){
        .name = "Synthwave 84",
        .desc = "Outrun aesthetic with magenta laser and radioactive cyan",
        .border = color_hex("#ff007f"),
        .header_bg = color_hex("#20092c"),
        .header_fg = color_hex("#00fff7"),
        .title = color_hex("#fe4450"),
        .accent = color_hex("#00fff7"),
        .accent2 = color_hex("#ff007f"),
        .cpu_color = color_hex("#00fff7"),
        .gpu_color = color_hex("#ff007f"),
        .mem_color = color_hex("#ffea00"),
        .net_down = color_hex("#72f1b8"),
        .net_up = color_hex("#ff8b39"),
        .text = color_hex("#fbfbfc"),
        .dim = color_hex("#614d85"),
        .warn = color_hex("#ffea00"),
        .crit = color_hex("#fe4450"),
        .proc_sel_bg = color_hex("#37174d"),
        .proc_sel_fg = color_hex("#00fff7")
    });
}

int theme_get_count(void) {
    return s_theme_count;
}

const Theme *theme_get(int index) {
    if (index >= 0 && index < s_theme_count) {
        return &s_themes[index];
    }
    return &s_themes[0];
}

const Theme *theme_current(void) {
    return theme_get(s_current_theme_index);
}

int theme_current_index(void) {
    return s_current_theme_index;
}

bool theme_set_by_index(int index) {
    if (index >= 0 && index < s_theme_count) {
        s_current_theme_index = index;
        return true;
    }
    return false;
}

bool theme_set_by_name(const char *name) {
    if (!name) return false;
    for (int i = 0; i < s_theme_count; i++) {
        if (strcasecmp(s_themes[i].name, name) == 0) {
            s_current_theme_index = i;
            return true;
        }
    }
    return false;
}

void theme_cycle_next(void) {
    if (s_theme_count > 0) {
        s_current_theme_index = (s_current_theme_index + 1) % s_theme_count;
    }
}

void theme_cycle_prev(void) {
    if (s_theme_count > 0) {
        s_current_theme_index = (s_current_theme_index - 1 + s_theme_count) % s_theme_count;
    }
}

// Lightweight JSON property extractor
static void extract_json_str(const char *json, const char *key, char *out, size_t max) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *pos = strstr(json, pattern);
    if (!pos) return;
    pos = strchr(pos, ':');
    if (!pos) return;
    pos = strchr(pos, '\"');
    if (!pos) return;
    pos++;
    const char *end = strchr(pos, '\"');
    if (!end) return;
    size_t len = end - pos;
    if (len >= max) len = max - 1;
    strncpy(out, pos, len);
    out[len] = '\0';
}

bool theme_load_from_json(const char *filepath) {
    if (s_theme_count >= MAX_THEMES) return false;
    FILE *f = fopen(filepath, "r");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 65536) {
        fclose(f);
        return false;
    }

    char *buf = malloc(sz + 1);
    if (!buf) { fclose(f); return false; }
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);

    char name[64] = "Custom Theme";
    char desc[128] = "User custom theme loaded from JSON";
    char border[16] = "#00f0ff", header_bg[16] = "#1f2335", header_fg[16] = "#ffffff";
    char title[16] = "#ffe600", accent[16] = "#00f0ff", accent2[16] = "#ff007f";
    char cpu[16] = "#00f0ff", gpu[16] = "#ff007f", mem[16] = "#ffe600";
    char net_down[16] = "#00ff66", net_up[16] = "#ff7700";
    char text[16] = "#ffffff", dim[16] = "#666688", warn[16] = "#ffe600", crit[16] = "#ff003c";
    char sel_bg[16] = "#332244", sel_fg[16] = "#ffffff";

    extract_json_str(buf, "name", name, sizeof(name));
    extract_json_str(buf, "desc", desc, sizeof(desc));
    extract_json_str(buf, "border", border, sizeof(border));
    extract_json_str(buf, "header_bg", header_bg, sizeof(header_bg));
    extract_json_str(buf, "header_fg", header_fg, sizeof(header_fg));
    extract_json_str(buf, "title", title, sizeof(title));
    extract_json_str(buf, "accent", accent, sizeof(accent));
    extract_json_str(buf, "accent2", accent2, sizeof(accent2));
    extract_json_str(buf, "cpu_color", cpu, sizeof(cpu));
    extract_json_str(buf, "gpu_color", gpu, sizeof(gpu));
    extract_json_str(buf, "mem_color", mem, sizeof(mem));
    extract_json_str(buf, "net_down", net_down, sizeof(net_down));
    extract_json_str(buf, "net_up", net_up, sizeof(net_up));
    extract_json_str(buf, "text", text, sizeof(text));
    extract_json_str(buf, "dim", dim, sizeof(dim));
    extract_json_str(buf, "warn", warn, sizeof(warn));
    extract_json_str(buf, "crit", crit, sizeof(crit));
    extract_json_str(buf, "proc_sel_bg", sel_bg, sizeof(sel_bg));
    extract_json_str(buf, "proc_sel_fg", sel_fg, sizeof(sel_fg));

    free(buf);

    Theme t = {
        .name = strdup(name),
        .desc = strdup(desc),
        .border = color_hex(border),
        .header_bg = color_hex(header_bg),
        .header_fg = color_hex(header_fg),
        .title = color_hex(title),
        .accent = color_hex(accent),
        .accent2 = color_hex(accent2),
        .cpu_color = color_hex(cpu),
        .gpu_color = color_hex(gpu),
        .mem_color = color_hex(mem),
        .net_down = color_hex(net_down),
        .net_up = color_hex(net_up),
        .text = color_hex(text),
        .dim = color_hex(dim),
        .warn = color_hex(warn),
        .crit = color_hex(crit),
        .proc_sel_bg = color_hex(sel_bg),
        .proc_sel_fg = color_hex(sel_fg)
    };

    // Check if theme with same name already exists; if so, update it
    for (int i = 0; i < s_theme_count; i++) {
        if (strcasecmp(s_themes[i].name, name) == 0) {
            s_themes[i] = t;
            return true;
        }
    }

    add_theme(t);
    return true;
}

void theme_load_user_themes(void) {
    const char *home = getenv("HOME");
    char path[512];
    if (home) {
        snprintf(path, sizeof(path), "%s/.config/macpulse/themes", home);
        DIR *d = opendir(path);
        if (d) {
            struct dirent *ent;
            while ((ent = readdir(d)) != NULL) {
                if (strstr(ent->d_name, ".json")) {
                    char full[1024];
                    snprintf(full, sizeof(full), "%s/%s", path, ent->d_name);
                    theme_load_from_json(full);
                }
            }
            closedir(d);
        }
    }

    // Also check local ./themes directory
    DIR *d2 = opendir("themes");
    if (d2) {
        struct dirent *ent;
        while ((ent = readdir(d2)) != NULL) {
            if (strstr(ent->d_name, ".json")) {
                char full[1024];
                snprintf(full, sizeof(full), "themes/%s", ent->d_name);
                theme_load_from_json(full);
            }
        }
        closedir(d2);
    }
}
