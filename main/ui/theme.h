#pragma once
// Screen themes. The screen UI is written against the dark "Forge" palette;
// the other themes remap those colours instead of carrying a second copy of
// every page. Include this after lvgl.h in the UI sources only: it reroutes
// lv_color_hex(), label text (for inline "#rrggbb " recolor codes) and shadows
// through the active theme. Fixed colours that must not change (QR codes) use
// lv_color_black()/lv_color_white().
#include "lvgl.h"

namespace theme {

enum Id : uint8_t { FORGE = 0, PAPER = 1, EMBER = 2 };

Id current();         // from settings, read once (a theme change reboots)
bool paper();
bool ember();
uint32_t map(uint32_t rgb);
lv_color_t color(uint32_t rgb);
void set_text(lv_obj_t *label, const char *text);
void set_text_fmt(lv_obj_t *label, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
lv_coord_t shadow(lv_coord_t width);
void apply_lvgl_theme(lv_disp_t *disp);  // light base theme for Paper, orange accent for Ember

} // namespace theme

#ifndef THEME_IMPL
#define lv_color_hex(c) theme::color(c)
#define lv_label_set_text(obj, txt) theme::set_text(obj, txt)
#define lv_label_set_text_fmt(obj, ...) theme::set_text_fmt(obj, __VA_ARGS__)
#define lv_obj_set_style_shadow_width(obj, w, sel) lv_obj_set_style_shadow_width(obj, theme::shadow(w), sel)
#endif
