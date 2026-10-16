#ifndef MACPULSE_THEME_H
#define MACPULSE_THEME_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} ColorRGB;

typedef struct {
    const char *name;
    const char *desc;
    ColorRGB border;
    ColorRGB header_bg;
    ColorRGB header_fg;
    ColorRGB title;
    ColorRGB accent;
    ColorRGB accent2;
    ColorRGB cpu_color;
    ColorRGB gpu_color;
    ColorRGB mem_color;
    ColorRGB net_down;
    ColorRGB net_up;
    ColorRGB text;
    ColorRGB dim;
    ColorRGB warn;
    ColorRGB crit;
    ColorRGB proc_sel_bg;
    ColorRGB proc_sel_fg;
} Theme;

#define MAX_THEMES 32

void theme_init_defaults(void);
int theme_get_count(void);
const Theme *theme_get(int index);
const Theme *theme_current(void);
bool theme_set_by_index(int index);
bool theme_set_by_name(const char *name);
void theme_cycle_next(void);
void theme_cycle_prev(void);
int theme_current_index(void);

// Custom theme loading from file
bool theme_load_from_json(const char *filepath);
void theme_load_user_themes(void);

// ANSI formatting helpers
void ansi_fg_rgb(char *buf, size_t max, ColorRGB c);
void ansi_bg_rgb(char *buf, size_t max, ColorRGB c);
ColorRGB color_hex(const char *hex);
ColorRGB color_lerp(ColorRGB a, ColorRGB b, float t);

#endif // MACPULSE_THEME_H
