#include "ui.h"

#include <stdio.h>
#include <string.h>
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "lvgl.h"
#include "sdkconfig.h"

#include "core/board.h"
#include "core/settings.h"
#include "core/state.h"
#include "core/tuner.h"
#include "mining/work.h"
#include "net/weather.h"
#include "ui/layout_store.h"
#include "ui/ui_small.h"
#include <time.h>
#include "cJSON.h"
#include "ui/display.h"

// ------------------------------------------------------------------ palette
#define C_BG lv_color_hex(0x070a10)
#define C_PANEL lv_color_hex(0x0f1520)
#define C_BORDER lv_color_hex(0x1c2635)
#define C_TEXT lv_color_hex(0xe6edf6)
#define C_DIM lv_color_hex(0x6b7a90)
#define C_CYAN lv_color_hex(0x00e5ff)
#define C_MAGENTA lv_color_hex(0xff2bd6)
#define C_GREEN lv_color_hex(0x2bff88)
#define C_ORANGE lv_color_hex(0xff9f1c)
#define C_RED lv_color_hex(0xff3b5c)
#define C_VIOLET lv_color_hex(0x8b5cff)

// Every page has a letter; settings.pages lists the enabled ones in order.
static const char PAGE_KINDS[] = "dcbpxsuv";
#define NUM_KINDS 8
#define K_DASH 0
#define K_CLOCK 1
#define K_BLOCKS 2
#define K_POOL 3
#define K_CHIPS 4
#define K_SYSTEM 5
#define K_CUSTOM1 6
#define K_CUSTOM2 7
#define C_SUN lv_color_hex(0xffc83d)
#define C_CLOUD lv_color_hex(0xc9d3e0)
#define C_RAIN lv_color_hex(0x3da5ff)
#define HEADER_H 34

LV_FONT_DECLARE(lv_font_montserrat_12);
LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_20);
LV_FONT_DECLARE(lv_font_montserrat_28);
LV_FONT_DECLARE(lv_font_montserrat_48);
LV_FONT_DECLARE(font_clock_110);
LV_FONT_DECLARE(font_temp_64);

static lv_obj_t *s_pages[NUM_KINDS];
static lv_obj_t *s_dots[NUM_KINDS];
static int s_order[NUM_KINDS];   // kinds in display order
static int s_order_n = 0;
static int s_page = 0;           // index into s_order
static volatile int s_req_page = 0;       // from other tasks: +1 / -1 / 100+kind
static volatile bool s_req_pages_reload = false;
static bool s_backlight = true;
static uint32_t s_last_input_ms = 0;  // pauses auto-rotate after a button press
static uint32_t s_page_since_ms = 0;

// clock page
static lv_obj_t *s_clk_hh, *s_clk_colon, *s_clk_mm, *s_clk_ampm, *s_clk_sec_bar;
static lv_obj_t *s_clk_wday, *s_clk_date, *s_clk_year, *s_clk_mining;
static lv_obj_t *s_wx_icon, *s_wx_temp, *s_wx_cond, *s_wx_details, *s_wx_city;
struct DayCard {
    lv_obj_t *name, *icon, *range;
};
static DayCard s_wx_days[3];
static int s_wx_drawn_code[4] = {-2, -2, -2, -2};

// header
static lv_obj_t *s_status_pill, *s_status_lbl, *s_status_dot, *s_ip_lbl, *s_uptime_lbl;
static lv_obj_t *s_banner, *s_banner_lbl;

// dashboard
static lv_obj_t *s_arc_hash, *s_arc_eff, *s_hash_val, *s_hash_unit, *s_hash_sub;
struct Tile {
    lv_obj_t *val, *sub, *bar;
};
static Tile s_t_chip, s_t_vr, s_t_pwr, s_t_fan;
static lv_obj_t *s_tune_pill, *s_tune_pill_lbl, *s_tune_note, *s_tune_bar, *s_tune_point;
static lv_obj_t *s_chart;
static lv_chart_series_t *s_series;

// chips page
struct ChipCard {
    lv_obj_t *ghs, *temp, *err, *bar;
};
static ChipCard s_chips[AF_NUM_ASICS];
static lv_obj_t *s_td_phase, *s_td_best, *s_td_trial, *s_td_steps, *s_td_note, *s_td_bar;


// system page
static lv_obj_t *s_qr, *s_sys_title, *s_sys_lines;
static char s_qr_text[192] = "";

// ------------------------------------------------------------------ helpers
static void fmt_hash(float ghs, char *val, size_t vl, const char **unit)
{
    if (ghs >= 1000.0f) {
        snprintf(val, vl, "%.2f", ghs / 1000.0f);
        *unit = "TH/s";
    } else {
        snprintf(val, vl, "%.0f", ghs);
        *unit = "GH/s";
    }
}

static void fmt_diff(double d, char *out, size_t len)
{
    const char *suf[] = {"", "k", "M", "G", "T", "P", "E"};
    int i = 0;
    while (d >= 1000.0 && i < 6) {
        d /= 1000.0;
        i++;
    }
    snprintf(out, len, i ? "%.2f%s" : "%.0f%s", d, suf[i]);
}

static lv_color_t temp_color(float t, float warn, float hot)
{
    if (t >= hot) return C_RED;
    if (t >= warn) return C_ORANGE;
    return C_GREEN;
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text);
    return l;
}

static lv_obj_t *panel(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_remove_style_all(p);
    lv_obj_set_pos(p, x, y);
    lv_obj_set_size(p, w, h);
    lv_obj_set_style_bg_color(p, C_PANEL, 0);
    lv_obj_set_style_bg_grad_color(p, lv_color_hex(0x0b0f17), 0);
    lv_obj_set_style_bg_grad_dir(p, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(p, C_BORDER, 0);
    lv_obj_set_style_border_width(p, 1, 0);
    lv_obj_set_style_radius(p, 10, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

static lv_obj_t *thin_bar(lv_obj_t *parent, lv_color_t color)
{
    lv_obj_t *b = lv_bar_create(parent);
    lv_obj_set_height(b, 4);
    lv_obj_set_style_bg_color(b, C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(b, color, LV_PART_INDICATOR);
    lv_obj_set_style_radius(b, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(b, 2, LV_PART_INDICATOR);
    lv_bar_set_range(b, 0, 100);
    return b;
}

static Tile make_tile(lv_obj_t *parent, int x, int y, int w, int h, const char *caption, lv_color_t accent)
{
    Tile t;
    lv_obj_t *p = panel(parent, x, y, w, h);
    lv_obj_t *cap = label(p, &lv_font_montserrat_12, C_DIM, caption);
    lv_obj_set_pos(cap, 10, 6);
    lv_obj_t *stripe = lv_obj_create(p);
    lv_obj_remove_style_all(stripe);
    lv_obj_set_size(stripe, 3, 14);
    lv_obj_set_pos(stripe, 0, 7);
    lv_obj_set_style_bg_color(stripe, accent, 0);
    lv_obj_set_style_bg_opa(stripe, LV_OPA_COVER, 0);
    t.val = label(p, &lv_font_montserrat_28, C_TEXT, "--");
    lv_obj_set_pos(t.val, 10, 20);
    t.sub = label(p, &lv_font_montserrat_12, C_DIM, "");
    lv_obj_align(t.sub, LV_ALIGN_TOP_RIGHT, -8, 6);
    t.bar = thin_bar(p, accent);
    lv_obj_set_width(t.bar, w - 20);
    lv_obj_align(t.bar, LV_ALIGN_BOTTOM_MID, 0, -8);
    return t;
}

static void pulse_anim(void *obj, int32_t v) { lv_obj_set_style_opa((lv_obj_t *) obj, (lv_opa_t) v, 0); }

static lv_obj_t *new_page(lv_obj_t *scr)
{
    lv_obj_t *p = lv_obj_create(scr);
    lv_obj_remove_style_all(p);
    lv_obj_set_pos(p, 0, HEADER_H);
    lv_obj_set_size(p, LCD_H_RES, LCD_V_RES - HEADER_H - 14);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    return p;
}

// ------------------------------------------------------------------ builders
static void build_header(lv_obj_t *scr)
{
    lv_obj_t *h = lv_obj_create(scr);
    lv_obj_remove_style_all(h);
    lv_obj_set_size(h, LCD_H_RES, HEADER_H);
    lv_obj_set_style_bg_color(h, lv_color_hex(0x0a0f18), 0);
    lv_obj_set_style_bg_opa(h, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(h, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(h, C_CYAN, 0);
    lv_obj_set_style_border_opa(h, LV_OPA_40, 0);
    lv_obj_set_style_border_width(h, 1, 0);

    lv_obj_t *logo = lv_label_create(h);
    lv_label_set_recolor(logo, true);
    lv_obj_set_style_text_font(logo, &lv_font_montserrat_20, 0);
    lv_label_set_text(logo, "#00e5ff AXE##ff2bd6 FORGE#");
    lv_obj_align(logo, LV_ALIGN_LEFT_MID, 10, 0);

    s_status_pill = lv_obj_create(h);
    lv_obj_remove_style_all(s_status_pill);
    lv_obj_set_size(s_status_pill, 112, 22);
    lv_obj_align(s_status_pill, LV_ALIGN_LEFT_MID, 150, 0);
    lv_obj_set_style_radius(s_status_pill, 11, 0);
    lv_obj_set_style_bg_opa(s_status_pill, LV_OPA_20, 0);
    lv_obj_set_style_bg_color(s_status_pill, C_GREEN, 0);
    lv_obj_set_style_border_width(s_status_pill, 1, 0);
    lv_obj_set_style_border_color(s_status_pill, C_GREEN, 0);
    s_status_dot = lv_obj_create(s_status_pill);
    lv_obj_remove_style_all(s_status_dot);
    lv_obj_set_size(s_status_dot, 8, 8);
    lv_obj_set_style_radius(s_status_dot, 4, 0);
    lv_obj_set_style_bg_opa(s_status_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_status_dot, C_GREEN, 0);
    lv_obj_align(s_status_dot, LV_ALIGN_LEFT_MID, 9, 0);
    s_status_lbl = label(s_status_pill, &lv_font_montserrat_12, C_GREEN, "BOOTING");
    lv_obj_align(s_status_lbl, LV_ALIGN_LEFT_MID, 22, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_status_dot);
    lv_anim_set_exec_cb(&a, pulse_anim);
    lv_anim_set_values(&a, LV_OPA_20, LV_OPA_COVER);
    lv_anim_set_time(&a, 900);
    lv_anim_set_playback_time(&a, 900);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);

    s_uptime_lbl = label(h, &lv_font_montserrat_12, C_DIM, "");
    lv_obj_align(s_uptime_lbl, LV_ALIGN_RIGHT_MID, -10, 0);
    s_ip_lbl = label(h, &lv_font_montserrat_12, C_TEXT, "");
    lv_obj_align(s_ip_lbl, LV_ALIGN_RIGHT_MID, -80, 0);
}

static void build_dashboard(lv_obj_t *p)
{
    // --- hashrate gauge ---
    lv_obj_t *gp = panel(p, 6, 4, 202, 200);
    s_arc_hash = lv_arc_create(gp);
    lv_obj_set_size(s_arc_hash, 184, 184);
    lv_obj_center(s_arc_hash);
    lv_arc_set_bg_angles(s_arc_hash, 135, 45);
    lv_arc_set_range(s_arc_hash, 0, 1000);
    lv_obj_remove_style(s_arc_hash, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_arc_hash, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_arc_hash, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc_hash, C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc_hash, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc_hash, C_CYAN, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_arc_hash, true, LV_PART_INDICATOR);

    s_arc_eff = lv_arc_create(gp);
    lv_obj_set_size(s_arc_eff, 148, 148);
    lv_obj_center(s_arc_eff);
    lv_arc_set_bg_angles(s_arc_eff, 135, 45);
    lv_arc_set_range(s_arc_eff, 0, 100);
    lv_obj_remove_style(s_arc_eff, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_arc_eff, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_arc_eff, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc_eff, C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc_eff, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc_eff, C_MAGENTA, LV_PART_INDICATOR);

    s_hash_val = label(gp, &lv_font_montserrat_48, C_TEXT, "--");
    lv_obj_align(s_hash_val, LV_ALIGN_CENTER, 0, -8);
    s_hash_unit = label(gp, &lv_font_montserrat_20, C_CYAN, "TH/s");
    lv_obj_align(s_hash_unit, LV_ALIGN_CENTER, 0, 28);
    s_hash_sub = label(gp, &lv_font_montserrat_12, C_DIM, "");
    lv_obj_align(s_hash_sub, LV_ALIGN_BOTTOM_MID, 0, -6);

    // --- stat tiles ---
    const int tx = 214, tw = 126, th = 70;
    s_t_chip = make_tile(p, tx, 4, tw, th, "CHIP TEMP", C_ORANGE);
    s_t_vr = make_tile(p, tx + tw + 6, 4, tw, th, "VR TEMP", C_VIOLET);
    s_t_pwr = make_tile(p, tx, 80, tw, th, "POWER", C_MAGENTA);
    s_t_fan = make_tile(p, tx + tw + 6, 80, tw, th, "FAN", C_CYAN);

    // --- auto-tune strip ---
    lv_obj_t *tp = panel(p, tx, 156, tw * 2 + 6, 48);
    s_tune_pill = lv_obj_create(tp);
    lv_obj_remove_style_all(s_tune_pill);
    lv_obj_set_size(s_tune_pill, 74, 18);
    lv_obj_set_pos(s_tune_pill, 8, 6);
    lv_obj_set_style_radius(s_tune_pill, 9, 0);
    lv_obj_set_style_bg_opa(s_tune_pill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_tune_pill, C_VIOLET, 0);
    s_tune_pill_lbl = label(s_tune_pill, &lv_font_montserrat_12, lv_color_hex(0x000000), "AUTO");
    lv_obj_center(s_tune_pill_lbl);
    s_tune_point = label(tp, &lv_font_montserrat_14, C_TEXT, "");
    lv_obj_align(s_tune_point, LV_ALIGN_TOP_RIGHT, -8, 6);
    s_tune_note = label(tp, &lv_font_montserrat_12, C_DIM, "");
    lv_obj_set_width(s_tune_note, tw * 2 - 14);
    lv_label_set_long_mode(s_tune_note, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_pos(s_tune_note, 8, 24);
    s_tune_bar = thin_bar(tp, C_VIOLET);
    lv_obj_set_size(s_tune_bar, tw * 2 - 14, 3);
    lv_obj_align(s_tune_bar, LV_ALIGN_BOTTOM_MID, 0, -3);

    // --- hashrate history ---
    lv_obj_t *cp = panel(p, 6, 210, LCD_H_RES - 12, 60);
    lv_obj_t *cl = label(cp, &lv_font_montserrat_12, C_DIM, "HASHRATE  1h");
    lv_obj_set_pos(cl, 8, 4);
    s_chart = lv_chart_create(cp);
    lv_obj_set_size(s_chart, LCD_H_RES - 24, 40);
    lv_obj_align(s_chart, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_opa(s_chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_chart, 0, 0);
    lv_obj_set_style_pad_all(s_chart, 0, 0);
    lv_obj_set_style_line_color(s_chart, C_BORDER, LV_PART_MAIN);
    lv_chart_set_div_line_count(s_chart, 3, 0);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chart, AF_HISTORY_LEN);
    lv_chart_set_update_mode(s_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_style_size(s_chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(s_chart, 2, LV_PART_ITEMS);
    s_series = lv_chart_add_series(s_chart, C_CYAN, LV_CHART_AXIS_PRIMARY_Y);
}

// ------------------------------------------------------------------ weather icons
// Icons are drawn from primitives so no image assets are needed.
static lv_obj_t *blob(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t c, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static void draw_sun(lv_obj_t *box, int sz, int x, int y, int d)
{
    lv_obj_t *s = blob(box, x, y, d, d, C_SUN, LV_RADIUS_CIRCLE);
    lv_obj_set_style_shadow_color(s, C_SUN, 0);
    lv_obj_set_style_shadow_width(s, sz / 4, 0);
    lv_obj_set_style_shadow_opa(s, LV_OPA_60, 0);
}

static void draw_moon(lv_obj_t *box, int x, int y, int d, lv_color_t bg)
{
    lv_obj_t *m = blob(box, x, y, d, d, lv_color_hex(0xe8ecf5), LV_RADIUS_CIRCLE);
    lv_obj_set_style_shadow_color(m, lv_color_hex(0x9fb4ff), 0);
    lv_obj_set_style_shadow_width(m, d / 3, 0);
    lv_obj_set_style_shadow_opa(m, LV_OPA_40, 0);
    blob(box, x + d / 3, y - d / 8, d, d, bg, LV_RADIUS_CIRCLE);  // bite out a crescent
}

static void draw_cloud(lv_obj_t *box, int sz, int y, lv_color_t c)
{
    int w = sz * 8 / 10, h = sz * 28 / 100;
    int x = (sz - w) / 2;
    blob(box, x + w / 8, y - h * 6 / 10, h * 12 / 10, h * 12 / 10, c, LV_RADIUS_CIRCLE);
    blob(box, x + w * 4 / 10, y - h, h * 16 / 10, h * 16 / 10, c, LV_RADIUS_CIRCLE);
    blob(box, x, y, w, h, c, h / 2);
}

static void draw_drops(lv_obj_t *box, int sz, int y, lv_color_t c, bool dots)
{
    for (int i = 0; i < 3; i++) {
        int x = sz * (28 + i * 18) / 100;
        int yy = y + (i % 2) * sz / 14;
        if (dots) blob(box, x, yy, sz / 11, sz / 11, c, LV_RADIUS_CIRCLE);
        else blob(box, x, yy, sz / 22 + 1, sz / 6, c, 2);
    }
}

static void draw_bolt(lv_obj_t *box, int sz, int y)
{
    // lv_line keeps a pointer to its points, so they live with the object
    lv_point_t *p = (lv_point_t *) lv_mem_alloc(4 * sizeof(lv_point_t));
    p[0] = {(lv_coord_t) (sz * 55 / 100), (lv_coord_t) y};
    p[1] = {(lv_coord_t) (sz * 42 / 100), (lv_coord_t) (y + sz / 7)};
    p[2] = {(lv_coord_t) (sz * 56 / 100), (lv_coord_t) (y + sz / 7)};
    p[3] = {(lv_coord_t) (sz * 44 / 100), (lv_coord_t) (y + sz * 3 / 10)};
    lv_obj_t *l = lv_line_create(box);
    lv_line_set_points(l, p, 4);
    lv_obj_set_style_line_width(l, sz / 16 + 1, 0);
    lv_obj_set_style_line_color(l, C_SUN, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_add_event_cb(l, [](lv_event_t *e) { lv_mem_free(lv_event_get_user_data(e)); }, LV_EVENT_DELETE, p);
}

static void draw_weather_icon(lv_obj_t *box, int code, bool day, lv_color_t bg)
{
    lv_obj_clean(box);
    int sz = lv_obj_get_width(box);
    int base = sz * 52 / 100;
    if (code < 0) {
        lv_obj_t *q = label(box, &lv_font_montserrat_20, C_DIM, "?");
        lv_obj_center(q);
        return;
    }
    bool clear = code <= 1, partly = code == 2;
    if (clear || partly) {
        int d = clear ? sz * 55 / 100 : sz * 42 / 100;
        int x = clear ? (sz - d) / 2 : sz * 45 / 100, y = clear ? (sz - d) / 2 : sz * 8 / 100;
        if (day) draw_sun(box, sz, x, y, d);
        else draw_moon(box, x, y, d, bg);
        if (partly) draw_cloud(box, sz, base + sz / 10, C_CLOUD);
        return;
    }
    if (code == 45 || code == 48) {
        for (int i = 0; i < 3; i++)
            blob(box, sz / 8 + (i % 2) * sz / 12, sz * (30 + i * 16) / 100, sz * 3 / 4 - (i % 2) * sz / 8, sz / 12, C_CLOUD,
                 sz / 24);
        return;
    }
    bool dark = code >= 61;
    draw_cloud(box, sz, base - sz / 10, dark ? lv_color_hex(0x8d99ab) : C_CLOUD);
    int below = base - sz / 10 + sz * 30 / 100;
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) draw_drops(box, sz, below, C_RAIN, false);
    else if ((code >= 71 && code <= 77) || code == 85 || code == 86) draw_drops(box, sz, below, lv_color_hex(0xffffff), true);
    else if (code >= 95) draw_bolt(box, sz, below - sz / 20);
}

static lv_obj_t *icon_box(lv_obj_t *parent, int x, int y, int sz)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, sz, sz);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    return b;
}

#define C_DAYCARD lv_color_hex(0x141c2a)

static void build_clock(lv_obj_t *p)
{
    // big time: HH : MM as three labels so the colon can blink
    s_clk_hh = label(p, &font_clock_110, C_TEXT, "--");
    lv_obj_set_pos(s_clk_hh, 8, 14);
    s_clk_colon = label(p, &font_clock_110, C_CYAN, ":");
    s_clk_mm = label(p, &font_clock_110, C_TEXT, "--");
    s_clk_ampm = label(p, &lv_font_montserrat_14, C_CYAN, "");

    // seconds sweep under the time
    s_clk_sec_bar = thin_bar(p, C_CYAN);
    lv_obj_set_size(s_clk_sec_bar, 316, 3);
    lv_obj_set_pos(s_clk_sec_bar, 12, 104);
    lv_bar_set_range(s_clk_sec_bar, 0, 59);
    lv_obj_set_style_shadow_color(s_clk_sec_bar, C_CYAN, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_width(s_clk_sec_bar, 8, LV_PART_INDICATOR);

    // date column
    s_clk_wday = label(p, &lv_font_montserrat_20, C_MAGENTA, "");
    lv_obj_set_pos(s_clk_wday, 344, 16);
    s_clk_date = label(p, &lv_font_montserrat_28, C_TEXT, "");
    lv_obj_set_pos(s_clk_date, 344, 40);
    s_clk_year = label(p, &lv_font_montserrat_14, C_DIM, "");
    lv_obj_set_pos(s_clk_year, 344, 72);
    s_clk_mining = label(p, &lv_font_montserrat_12, C_CYAN, "");
    lv_obj_set_pos(s_clk_mining, 344, 94);

    // weather panel
    lv_obj_t *w = panel(p, 6, 116, LCD_H_RES - 12, 154);
    s_wx_icon = icon_box(w, 8, 8, 78);
    s_wx_temp = label(w, &font_temp_64, C_TEXT, "--");
    lv_obj_set_pos(s_wx_temp, 94, 12);
    s_wx_city = label(w, &lv_font_montserrat_12, C_CYAN, "");
    lv_obj_set_pos(s_wx_city, 96, 68);
    s_wx_cond = label(w, &lv_font_montserrat_20, C_TEXT, "");
    lv_obj_set_pos(s_wx_cond, 12, 94);
    s_wx_details = label(w, &lv_font_montserrat_12, C_DIM, "");
    lv_obj_set_width(s_wx_details, 236);
    lv_label_set_long_mode(s_wx_details, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(s_wx_details, 12, 122);

    // 3-day forecast
    for (int i = 0; i < 3; i++) {
        lv_obj_t *col = lv_obj_create(w);
        lv_obj_remove_style_all(col);
        lv_obj_set_pos(col, 252 + i * 70, 8);
        lv_obj_set_size(col, 64, 136);
        lv_obj_set_style_radius(col, 8, 0);
        lv_obj_set_style_bg_color(col, C_DAYCARD, 0);
        lv_obj_set_style_bg_opa(col, LV_OPA_COVER, 0);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
        s_wx_days[i].name = label(col, &lv_font_montserrat_12, C_DIM, i == 0 ? "Today" : "");
        lv_obj_align(s_wx_days[i].name, LV_ALIGN_TOP_MID, 0, 8);
        s_wx_days[i].icon = icon_box(col, 8, 30, 48);
        s_wx_days[i].range = lv_label_create(col);
        lv_label_set_recolor(s_wx_days[i].range, true);
        lv_obj_set_style_text_font(s_wx_days[i].range, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_wx_days[i].range, C_TEXT, 0);
        lv_obj_set_style_text_align(s_wx_days[i].range, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(s_wx_days[i].range, 64);
        lv_obj_align(s_wx_days[i].range, LV_ALIGN_BOTTOM_MID, 0, -8);
        lv_label_set_text(s_wx_days[i].range, "");
    }
}

static void build_chips(lv_obj_t *p)
{
    const int w = 112, gap = 4;
    for (int i = 0; i < AF_NUM_ASICS; i++) {
        lv_obj_t *c = panel(p, 6 + i * (w + gap), 4, w, 128);
        char cap[16];
        snprintf(cap, sizeof(cap), "ASIC %d", i + 1);
        lv_obj_t *l = label(c, &lv_font_montserrat_12, C_DIM, cap);
        lv_obj_set_pos(l, 10, 6);
        s_chips[i].ghs = label(c, &lv_font_montserrat_28, C_CYAN, "--");
        lv_obj_set_pos(s_chips[i].ghs, 10, 22);
        lv_obj_t *u = label(c, &lv_font_montserrat_12, C_DIM, "GH/s");
        lv_obj_set_pos(u, 10, 54);
        s_chips[i].temp = label(c, &lv_font_montserrat_14, C_GREEN, "");
        lv_obj_set_pos(s_chips[i].temp, 10, 76);
        s_chips[i].err = label(c, &lv_font_montserrat_12, C_DIM, "");
        lv_obj_set_pos(s_chips[i].err, 10, 96);
        s_chips[i].bar = thin_bar(c, C_CYAN);
        lv_obj_set_width(s_chips[i].bar, w - 20);
        lv_obj_align(s_chips[i].bar, LV_ALIGN_BOTTOM_MID, 0, -8);
    }

    lv_obj_t *t = panel(p, 6, 138, LCD_H_RES - 12, 132);
    lv_obj_t *cap = label(t, &lv_font_montserrat_12, C_VIOLET, LV_SYMBOL_SETTINGS "  AUTO-TUNE  (hold BTN2 to re-tune)");
    lv_obj_set_pos(cap, 10, 6);
    s_td_phase = label(t, &lv_font_montserrat_20, C_TEXT, "");
    lv_obj_set_pos(s_td_phase, 10, 26);
    s_td_best = label(t, &lv_font_montserrat_14, C_GREEN, "");
    lv_obj_set_pos(s_td_best, 10, 56);
    s_td_trial = label(t, &lv_font_montserrat_14, C_TEXT, "");
    lv_obj_set_pos(s_td_trial, 240, 56);
    s_td_steps = label(t, &lv_font_montserrat_12, C_DIM, "");
    lv_obj_set_pos(s_td_steps, 240, 30);
    s_td_note = label(t, &lv_font_montserrat_12, C_DIM, "");
    lv_obj_set_pos(s_td_note, 10, 82);
    s_td_bar = thin_bar(t, C_VIOLET);
    lv_obj_set_width(s_td_bar, LCD_H_RES - 40);
    lv_obj_align(s_td_bar, LV_ALIGN_BOTTOM_MID, 0, -12);
}

static void build_system(lv_obj_t *p)
{
    lv_obj_t *pn = panel(p, 6, 4, LCD_H_RES - 12, 266);
    s_qr = lv_qrcode_create(pn, 150, lv_color_hex(0x000000), lv_color_hex(0xffffff));
    lv_obj_align(s_qr, LV_ALIGN_LEFT_MID, 14, 0);
    lv_obj_set_style_border_color(s_qr, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_border_width(s_qr, 6, 0);
    s_sys_title = label(pn, &lv_font_montserrat_20, C_CYAN, "");
    lv_obj_set_pos(s_sys_title, 186, 18);
    s_sys_lines = lv_label_create(pn);
    lv_label_set_recolor(s_sys_lines, true);
    lv_obj_set_style_text_font(s_sys_lines, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_sys_lines, C_TEXT, 0);
    lv_obj_set_style_text_line_space(s_sys_lines, 6, 0);
    lv_obj_set_width(s_sys_lines, 270);
    lv_obj_set_pos(s_sys_lines, 186, 52);
}

static int kind_of(char c)
{
    const char *f = strchr(PAGE_KINDS, c);
    return (f && c) ? (int) (f - PAGE_KINDS) : -1;
}

static void load_order()
{
    s_order_n = 0;
    bool used[NUM_KINDS] = {};
    for (const char *c = settings::get().pages; *c && s_order_n < NUM_KINDS; c++) {
        int k = kind_of(*c);
        if (k >= 0 && !used[k]) {
            used[k] = true;
            s_order[s_order_n++] = k;
        }
    }
    if (!used[K_SYSTEM]) s_order[s_order_n++] = K_SYSTEM;  // setup QR must stay reachable
    int x0 = LCD_H_RES / 2 - (s_order_n * 16) / 2;
    for (int i = 0; i < NUM_KINDS; i++) {
        if (i < s_order_n) {
            lv_obj_clear_flag(s_dots[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(s_dots[i], x0 + i * 16, LCD_V_RES - 9);
        } else {
            lv_obj_add_flag(s_dots[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void show_page(int idx)
{
    s_page = ((idx % s_order_n) + s_order_n) % s_order_n;
    s_page_since_ms = lv_tick_get();
    for (int k = 0; k < NUM_KINDS; k++) lv_obj_add_flag(s_pages[k], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_pages[s_order[s_page]], LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < s_order_n; i++) {
        lv_obj_set_style_bg_color(s_dots[i], i == s_page ? C_CYAN : C_BORDER, 0);
        lv_obj_set_width(s_dots[i], i == s_page ? 18 : 6);
    }
}

static void show_kind(int kind)
{
    for (int i = 0; i < s_order_n; i++)
        if (s_order[i] == kind) show_page(i);
}

// ------------------------------------------------------------------ refresh
static void set_status(const char *text, lv_color_t c)
{
    lv_label_set_text(s_status_lbl, text);
    lv_obj_set_style_text_color(s_status_lbl, c, 0);
    lv_obj_set_style_bg_color(s_status_pill, c, 0);
    lv_obj_set_style_border_color(s_status_pill, c, 0);
    lv_obj_set_style_bg_color(s_status_dot, c, 0);
}

static const char *phase_name(TunerPhase p)
{
    switch (p) {
    case TunerPhase::Off: return "MANUAL";
    case TunerPhase::WarmUp: return "WARMUP";
    case TunerPhase::Measuring: return "TESTING";
    case TunerPhase::StepUp: return "STEP UP";
    case TunerPhase::Locked: return "LOCKED";
    case TunerPhase::Backoff: return "BACKOFF";
    }
    return "";
}

static void refresh_header()
{
    const MinerState &s = g_state;
    switch (s.status) {
    case SysStatus::Booting: set_status("BOOTING", C_DIM); break;
    case SysStatus::NoWifi: set_status("NO WIFI", C_ORANGE); break;
    case SysStatus::SetupAP: set_status("SETUP", C_VIOLET); break;
    case SysStatus::Connecting: set_status("CONNECTING", C_ORANGE); break;
    case SysStatus::Mining: set_status("MINING", C_GREEN); break;
    case SysStatus::Fault: set_status("FAULT", C_RED); break;
    case SysStatus::Overheat: set_status("OVERHEAT", C_RED); break;
    }
    lv_label_set_text_fmt(s_ip_lbl, LV_SYMBOL_WIFI " %s", s.ip[0] ? s.ip : (s.ap_ssid[0] ? "192.168.4.1" : "--"));
    uint32_t u = s.uptime_s;
    if (u >= 86400) lv_label_set_text_fmt(s_uptime_lbl, "%lud %02luh", (unsigned long) (u / 86400), (unsigned long) (u % 86400 / 3600));
    else lv_label_set_text_fmt(s_uptime_lbl, "%02lu:%02lu:%02lu", (unsigned long) (u / 3600), (unsigned long) (u % 3600 / 60), (unsigned long) (u % 60));

    bool alarm = s.status == SysStatus::Fault || s.status == SysStatus::Overheat;
    static uint32_t last_blocks = 0;
    static int block_flash = 0;
    if (s.blocks_found != last_blocks) {
        last_blocks = s.blocks_found;
        block_flash = 60;
    }
    if (block_flash > 0) {
        block_flash--;
        lv_obj_clear_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(s_banner, (block_flash & 2) ? C_GREEN : C_MAGENTA, 0);
        lv_label_set_text(s_banner_lbl, LV_SYMBOL_OK "  BLOCK FOUND!  " LV_SYMBOL_OK);
    } else if (alarm && s.fault[0]) {
        lv_obj_clear_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(s_banner, C_RED, 0);
        lv_label_set_text_fmt(s_banner_lbl, LV_SYMBOL_WARNING "  %s", s.fault);
    } else {
        lv_obj_add_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
    }
}

static void refresh_dashboard()
{
    const MinerState &s = g_state;
    char v[16];
    const char *unit;
    fmt_hash(s.hashrate_ghs, v, sizeof(v), &unit);
    lv_label_set_text(s_hash_val, v);
    lv_label_set_text(s_hash_unit, unit);
    float max_ghs = board::limits().max_freq_mhz * 2040.0f * AF_NUM_ASICS / 1000.0f;
    lv_arc_set_value(s_arc_hash, (int16_t) (1000.0f * s.hashrate_ghs / max_ghs));
    int eff = s.expected_ghs > 0 ? (int) (100.0f * s.hashrate_ghs / s.expected_ghs) : 0;
    lv_arc_set_value(s_arc_eff, eff > 100 ? 100 : eff);
    char exp_s[16];
    const char *exp_u;
    fmt_hash(s.expected_ghs, exp_s, sizeof(exp_s), &exp_u);
    lv_label_set_text_fmt(s_hash_sub, "%d%% of %s %s  err %.2f%%", eff, exp_s, exp_u, s.error_pct);

    lv_label_set_text_fmt(s_t_chip.val, "%.0f\xC2\xB0", s.chip_temp_max);
    lv_obj_set_style_text_color(s_t_chip.val, temp_color(s.chip_temp_max, CONFIG_AF_MAX_CHIP_TEMP - 6, CONFIG_AF_MAX_CHIP_TEMP), 0);
    lv_bar_set_value(s_t_chip.bar, (int) (100 * s.chip_temp_max / CONFIG_AF_CRIT_CHIP_TEMP), LV_ANIM_ON);
    lv_label_set_text_fmt(s_t_chip.sub, "<%dÂ°", CONFIG_AF_MAX_CHIP_TEMP);

    lv_label_set_text_fmt(s_t_vr.val, "%.0f\xC2\xB0", s.vr_temp);
    lv_obj_set_style_text_color(s_t_vr.val, temp_color(s.vr_temp, board::limits().vr_ceil_c - 10, board::limits().vr_ceil_c), 0);
    lv_bar_set_value(s_t_vr.bar, (int) (100 * s.vr_temp / board::limits().vr_crit_c), LV_ANIM_ON);
    lv_label_set_text_fmt(s_t_vr.sub, "%.2fV", s.vout);

    lv_label_set_text_fmt(s_t_pwr.val, "%.0fW", s.pin);
    float jth = s.hashrate_ghs > 1 ? s.pin / (s.hashrate_ghs / 1000.0f) : 0;
    lv_label_set_text_fmt(s_t_pwr.sub, "%.1f J/TH", jth);
    lv_bar_set_value(s_t_pwr.bar, (int) (100 * s.pin / board::limits().max_power_w), LV_ANIM_ON);

    lv_label_set_text_fmt(s_t_fan.val, "%u%%", s.fan_pct);
    lv_label_set_text_fmt(s_t_fan.sub, "%u / %u rpm", s.fan_rpm[0], s.fan_rpm[1]);
    lv_bar_set_value(s_t_fan.bar, s.fan_pct, LV_ANIM_ON);

    const TunerState &t = s.tuner;
    lv_label_set_text(s_tune_pill_lbl, phase_name(t.phase));
    lv_color_t pc = t.phase == TunerPhase::Locked ? C_GREEN
                    : t.phase == TunerPhase::Backoff ? C_ORANGE
                    : t.phase == TunerPhase::Off ? C_DIM
                                                 : C_VIOLET;
    lv_obj_set_style_bg_color(s_tune_pill, pc, 0);
    lv_obj_set_style_bg_color(s_tune_bar, pc, LV_PART_INDICATOR);
    lv_label_set_text_fmt(s_tune_point, "%u MHz  %u.%02u V", s.freq_mhz, s.core_mv / 1000, (s.core_mv % 1000) / 10);
    lv_label_set_text(s_tune_note, t.note);
    lv_bar_set_value(s_tune_bar, t.phase == TunerPhase::Locked || t.phase == TunerPhase::Off ? 100 : t.progress, LV_ANIM_ON);

    // chart: rebuild from the ring buffer (cheap: 120 points)
    float vmax = 1;
    for (int i = 0; i < AF_HISTORY_LEN; i++)
        if (s.history[i] > vmax) vmax = s.history[i];
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, (lv_coord_t) (vmax * 1.1f));
    for (int i = 0; i < AF_HISTORY_LEN; i++) {
        float h = s.history[(s.history_head + i) % AF_HISTORY_LEN];
        s_series->y_points[i] = h > 0 ? (lv_coord_t) h : LV_CHART_POINT_NONE;
    }
    lv_chart_refresh(s_chart);
}

static void refresh_chips()
{
    const MinerState &s = g_state;
    float per_chip = s.expected_ghs / (s.chips_detected ? s.chips_detected : AF_NUM_ASICS);
    for (int i = 0; i < AF_NUM_ASICS; i++) {
        lv_label_set_text_fmt(s_chips[i].ghs, "%.0f", s.chip_ghs[i]);
        if (s.chip_temp_own[i]) lv_label_set_text_fmt(s_chips[i].temp, "%.0f\xC2\xB0" "C", s.chip_temp[i]);
        else lv_label_set_text_fmt(s_chips[i].temp, "~%.0f\xC2\xB0" "C shared", s.chip_temp[i]);
        lv_obj_set_style_text_color(s_chips[i].temp, temp_color(s.chip_temp[i], CONFIG_AF_MAX_CHIP_TEMP - 6, CONFIG_AF_MAX_CHIP_TEMP), 0);
        float tot = s.chip_ghs[i] + s.chip_err_ghs[i];
        lv_label_set_text_fmt(s_chips[i].err, "err %.2f%%", tot > 0 ? 100.0f * s.chip_err_ghs[i] / tot : 0.0f);
        lv_bar_set_value(s_chips[i].bar, per_chip > 0 ? (int) (100 * s.chip_ghs[i] / per_chip) : 0, LV_ANIM_ON);
    }
    const TunerState &t = s.tuner;
    lv_label_set_text(s_td_phase, phase_name(t.phase));
    if (t.best_freq) {
        char hv[16];
        const char *hu;
        fmt_hash(t.best_ghs, hv, sizeof(hv), &hu);
        lv_label_set_text_fmt(s_td_best, LV_SYMBOL_OK " Best  %u MHz @ %u mV  (%s %s)", t.best_freq, t.best_mv, hv, hu);
    } else {
        lv_label_set_text(s_td_best, "Best  searching...");
    }
    if (t.trial_freq) lv_label_set_text_fmt(s_td_trial, "Trial %u MHz @ %u mV", t.trial_freq, t.trial_mv);
    lv_label_set_text_fmt(s_td_steps, "%u steps tested", t.steps_done);
    lv_label_set_text(s_td_note, t.note);
    lv_bar_set_value(s_td_bar, t.progress, LV_ANIM_ON);
}

static void refresh_system()
{
    const MinerState &s = g_state;
    char qr[192];
    if (s.ap_ssid[0] && !s.ip[0]) {
        snprintf(qr, sizeof(qr), "WIFI:T:nopass;S:%s;;", s.ap_ssid);
        lv_label_set_text(s_sys_title, "SETUP");
        lv_label_set_text_fmt(s_sys_lines,
                              "1. Scan to join Wi-Fi\n#00e5ff %s#\n2. Open #ff2bd6 http://192.168.4.1#\n3. Enter Wi-Fi + pool,\n    then Save",
                              s.ap_ssid);
    } else {
        snprintf(qr, sizeof(qr), "http://%s/", s.ip);
        lv_label_set_text(s_sys_title, "SYSTEM");
        lv_label_set_text_fmt(s_sys_lines,
                              "#6b7a90 Web UI# http://%s\n#6b7a90 Wi-Fi# %s\n#6b7a90 Firmware# AxeForge %s\n"
                              "#6b7a90 Chips# %d x BM1370\n#6b7a90 Free heap# %u kB",
                              s.ip, s.ssid, esp_app_get_description()->version, s.chips_detected,
                              (unsigned) (esp_get_free_heap_size() / 1024));
    }
    if (strcmp(qr, s_qr_text) != 0) {
        strcpy(s_qr_text, qr);
        lv_qrcode_update(s_qr, s_qr_text, strlen(s_qr_text));
    }
}

static void refresh_clock()
{
    const MinerState &s = g_state;
    const Settings &cfg = settings::get();
    static const char *wdays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    static const char *wshort[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    struct tm t;
    bool have_time = weather::local_time(&t);
    if (have_time) {
        int h = t.tm_hour;
        if (!cfg.clock_24h) {
            lv_label_set_text(s_clk_ampm, h < 12 ? "AM" : "PM");
            h = h % 12 ? h % 12 : 12;
        } else {
            lv_label_set_text(s_clk_ampm, "");
        }
        lv_label_set_text_fmt(s_clk_hh, "%02d", h);
        lv_label_set_text_fmt(s_clk_mm, "%02d", t.tm_min);
        lv_obj_set_style_text_opa(s_clk_colon, (t.tm_sec & 1) ? LV_OPA_30 : LV_OPA_COVER, 0);
        lv_bar_set_value(s_clk_sec_bar, t.tm_sec, LV_ANIM_OFF);
        lv_label_set_text(s_clk_wday, wdays[t.tm_wday]);
        lv_label_set_text_fmt(s_clk_date, "%d %s", t.tm_mday, months[t.tm_mon]);
        lv_label_set_text_fmt(s_clk_year, "%d", t.tm_year + 1900);
    } else {
        lv_label_set_text(s_clk_hh, "--");
        lv_label_set_text(s_clk_mm, "--");
        lv_label_set_text(s_clk_wday, "Syncing");
        lv_label_set_text(s_clk_date, "time...");
        lv_label_set_text(s_clk_year, "");
    }
    lv_obj_align_to(s_clk_colon, s_clk_hh, LV_ALIGN_OUT_RIGHT_MID, 0, -6);
    lv_obj_align_to(s_clk_mm, s_clk_colon, LV_ALIGN_OUT_RIGHT_MID, 0, 6);
    lv_obj_align_to(s_clk_ampm, s_clk_mm, LV_ALIGN_OUT_RIGHT_TOP, 4, 6);

    char hv[16];
    const char *hu;
    fmt_hash(s.hashrate_ghs, hv, sizeof(hv), &hu);
    lv_label_set_text_fmt(s_clk_mining, LV_SYMBOL_CHARGE " %s %s  %.0f\xC2\xB0" "C", hv, hu, s.chip_temp_max);

    const WeatherState &w = s.weather;
    const char *unit = cfg.fahrenheit ? "F" : "C";
    if (w.valid) {
        lv_label_set_text_fmt(s_wx_temp, "%.0f\xC2\xB0", w.temp);
        lv_label_set_text(s_wx_cond, weather::describe(w.code));
        lv_label_set_text_fmt(s_wx_details, "Feels %.0f\xC2\xB0%s   %u%% hum   %.0f %s", w.feels, unit, w.humidity, w.wind,
                              cfg.fahrenheit ? "mph" : "km/h");
        lv_label_set_text_fmt(s_wx_city, LV_SYMBOL_GPS " %s", w.city);
        int key = w.code * 2 + (w.is_day ? 1 : 0);
        if (s_wx_drawn_code[0] != key) {
            s_wx_drawn_code[0] = key;
            draw_weather_icon(s_wx_icon, w.code, w.is_day, C_PANEL);
        }
        for (int i = 0; i < 3; i++) {
            if (i > 0 && have_time) lv_label_set_text(s_wx_days[i].name, wshort[(t.tm_wday + i) % 7]);
            lv_label_set_text_fmt(s_wx_days[i].range, "%.0f\xC2\xB0\n#6b7a90 %.0f\xC2\xB0#", w.days[i].tmax, w.days[i].tmin);
            if (s_wx_drawn_code[i + 1] != w.days[i].code) {
                s_wx_drawn_code[i + 1] = w.days[i].code;
                draw_weather_icon(s_wx_days[i].icon, w.days[i].code, true, C_DAYCARD);
            }
        }
    } else {
        lv_label_set_text(s_wx_temp, "--");
        lv_label_set_text(s_wx_cond, "No weather yet");
        lv_label_set_text(s_wx_details, w.error[0] ? w.error : "Fetching...");
        lv_label_set_text(s_wx_city, cfg.city);
        if (s_wx_drawn_code[0] != -1) {
            s_wx_drawn_code[0] = -1;
            draw_weather_icon(s_wx_icon, -1, true, C_PANEL);
        }
    }
}

#include "ui/pages_pool_blocks.inc"
#include "ui/widgets.inc"

static void refresh_cb(lv_timer_t *)
{
    // requests from the web server, applied on the LVGL thread
    if (s_req_pages_reload) {
        s_req_pages_reload = false;
        int kind = s_order[s_page];
        load_order();
        s_page = 0;
        show_kind(kind);
        show_page(s_page);
    }
    if (layout_store::take_dirty()) {
        build_custom(0);
        build_custom(1);
    }
    int req = s_req_page;
    if (req) {
        s_req_page = 0;
        if (req >= 100) show_kind(req - 100);
        else show_page(s_page + req);
    }

    refresh_header();
    switch (s_order[s_page]) {
    case K_DASH: refresh_dashboard(); break;
    case K_CLOCK: refresh_clock(); break;
    case K_BLOCKS: refresh_blocks(); break;
    case K_POOL: refresh_pool(); break;
    case K_CHIPS: refresh_chips(); break;
    case K_SYSTEM: refresh_system(); break;
    case K_CUSTOM1: refresh_custom(0); break;
    case K_CUSTOM2: refresh_custom(1); break;
    }

    // optional carousel; a button press pauses it for a minute
    uint8_t rot = settings::get().page_rotate_s;
    uint32_t now = lv_tick_get();
    if (rot && now - s_last_input_ms > 60000 && now - s_page_since_ms > rot * 1000u && !g_state.ap_ssid[0]) {
        int next = (s_page + 1) % s_order_n;
        if (s_order[next] == K_SYSTEM) next = (next + 1) % s_order_n;  // skip the system page
        show_page(next);
    }
}

// ------------------------------------------------------------------ public
namespace ui {

void build()
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_grad_color(scr, lv_color_hex(0x0b1220), 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    build_header(scr);
    for (int i = 0; i < NUM_KINDS; i++) s_pages[i] = new_page(scr);
    build_dashboard(s_pages[K_DASH]);
    build_clock(s_pages[K_CLOCK]);
    build_blocks(s_pages[K_BLOCKS]);
    build_pool(s_pages[K_POOL]);
    build_chips(s_pages[K_CHIPS]);
    build_system(s_pages[K_SYSTEM]);
    s_custom_root[0] = s_pages[K_CUSTOM1];
    s_custom_root[1] = s_pages[K_CUSTOM2];
    build_custom(0);
    build_custom(1);

    for (int i = 0; i < NUM_KINDS; i++) {
        s_dots[i] = lv_obj_create(scr);
        lv_obj_remove_style_all(s_dots[i]);
        lv_obj_set_size(s_dots[i], 6, 6);
        lv_obj_set_style_radius(s_dots[i], 3, 0);
        lv_obj_set_style_bg_opa(s_dots[i], LV_OPA_COVER, 0);
    }

    s_banner = lv_obj_create(scr);
    lv_obj_remove_style_all(s_banner);
    lv_obj_set_size(s_banner, LCD_H_RES, 30);
    lv_obj_align(s_banner, LV_ALIGN_TOP_MID, 0, HEADER_H);
    lv_obj_set_style_bg_opa(s_banner, LV_OPA_90, 0);
    s_banner_lbl = label(s_banner, &lv_font_montserrat_14, lv_color_hex(0xffffff), "");
    lv_obj_center(s_banner_lbl);
    lv_obj_add_flag(s_banner, LV_OBJ_FLAG_HIDDEN);

    // first boot without Wi-Fi: open on the setup QR
    load_order();
    if (settings::get().wifi_ssid[0]) show_page(settings::get().default_page % s_order_n);
    else show_kind(K_SYSTEM);
    lv_timer_create(refresh_cb, 500, NULL);
    refresh_cb(NULL);
}

void on_button(int index)
{
    s_last_input_ms = lv_tick_get();
    show_page(s_page + (index == 0 ? 1 : -1));
}

// Callers (web server) don't care which screen is fitted; route to the active UI.
void request_page(int delta)
{
    if (display::small()) return ui_small::request_page(delta);
    s_req_page = delta;
}
void request_kind(char kind)
{
    if (display::small()) return ui_small::request_kind(kind);
    int k = kind_of(kind);
    if (k >= 0) s_req_page = 100 + k;
}
void reload_pages()
{
    if (display::small()) return ui_small::reload_pages();
    s_req_pages_reload = true;
}
int current_page() { return s_page; }
char current_kind()
{
    if (display::small()) return ui_small::current_kind();
    return PAGE_KINDS[s_order[s_page]];
}

void on_long_press(int index)
{
    if (index == 0) {
        s_backlight = !s_backlight;
        display::set_brightness(s_backlight ? settings::get().brightness : 0);
    } else {
        tuner::restart();
    }
}

} // namespace ui
