#include "ui_small.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_app_desc.h"
#include "lvgl.h"
#include "ui/theme.h"  // after lvgl.h: routes colours through the active theme
#include "sdkconfig.h"

#include "asic/bm13xx.h"
#include "core/board.h"
#include "core/settings.h"
#include "core/state.h"
#include "core/tuner.h"
#include "mining/work.h"
#include "net/weather.h"
#include "ui/display.h"

#define W 320
#define H 170
#define HDR 22
#define PAGE_H (H - HDR - 8)

#define C_BG lv_color_hex(0x070a10)
#define C_PANEL lv_color_hex(0x0f1520)
#define C_BORDER lv_color_hex(0x1c2635)
// empty part of bars, arcs, chart grid and page dots: same shade as C_BORDER in
// Forge, but its own colour so other themes can lighten tracks and keep borders
#define C_TRACK lv_color_hex(0x1c2636)
#define C_TEXT lv_color_hex(0xe6edf6)
#define C_DIM lv_color_hex(0x9aa8bd)
#define C_CYAN lv_color_hex(0x00e5ff)
#define C_MAGENTA lv_color_hex(0xff2bd6)
#define C_GREEN lv_color_hex(0x2bff88)
#define C_ORANGE lv_color_hex(0xff9f1c)
#define C_RED lv_color_hex(0xff3b5c)
#define C_VIOLET lv_color_hex(0x8b5cff)

LV_FONT_DECLARE(lv_font_montserrat_12);
LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_20);
LV_FONT_DECLARE(lv_font_montserrat_28);
LV_FONT_DECLARE(font_clock_110);

// page letters shared with the 3.5" UI; the custom pages (u, v) need the big screen
static const char KINDS[] = "dcbpxs";
#define NUM_KINDS 6
#define K_DASH 0
#define K_CLOCK 1
#define K_BLOCKS 2
#define K_POOL 3
#define K_CHIPS 4
#define K_SYSTEM 5

static lv_obj_t *s_pages[NUM_KINDS], *s_dots[NUM_KINDS];
static int s_order[NUM_KINDS], s_order_n = 0, s_page = 0;
static volatile int s_req_page = 0;
static volatile bool s_req_reload = false;
static uint32_t s_last_input_ms = 0, s_page_since_ms = 0;
static bool s_backlight = true;

// header
static lv_obj_t *s_status, *s_status_dot, *s_hdr_right, *s_banner, *s_banner_lbl;
// dashboard
static lv_obj_t *s_arc, *s_hash, *s_unit, *s_dash_rows[4], *s_dash_vals[4], *s_tune;
// clock
static lv_obj_t *s_clk, *s_clk_date, *s_clk_wx, *s_clk_sec;
// blocks
static lv_obj_t *s_cube[4], *s_cube_lbl[4], *s_blk_now, *s_blk_hash;
// pool
static lv_obj_t *s_pool_tot, *s_pool_rows[AF_POOLS];
// chips
static lv_obj_t *s_chip_ghs[AF_NUM_ASICS], *s_chip_sub[AF_NUM_ASICS], *s_chip_bar[AF_NUM_ASICS];
// system
static lv_obj_t *s_qr, *s_sys;
static char s_qr_text[96] = "";

// ------------------------------------------------------------------ helpers
static lv_obj_t *label(lv_obj_t *p, const lv_font_t *f, lv_color_t c, const char *t)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, t);
    return l;
}

static lv_obj_t *rlabel(lv_obj_t *p, const lv_font_t *f, lv_color_t c)
{
    lv_obj_t *l = label(p, f, c, "");
    lv_label_set_recolor(l, true);
    return l;
}

static lv_obj_t *box(lv_obj_t *p, int x, int y, int w, int h, lv_color_t c, int r)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *panel(lv_obj_t *p, int x, int y, int w, int h)
{
    lv_obj_t *o = box(p, x, y, w, h, C_PANEL, 8);
    lv_obj_set_style_border_color(o, C_BORDER, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    return o;
}

static lv_obj_t *bar(lv_obj_t *p, int w, lv_color_t c)
{
    lv_obj_t *b = lv_bar_create(p);
    lv_obj_set_size(b, w, 3);
    lv_obj_set_style_bg_color(b, C_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(b, c, LV_PART_INDICATOR);
    lv_bar_set_range(b, 0, 100);
    return b;
}

static void fmt_hash(float ghs, char *v, size_t n, const char **u)
{
    if (ghs >= 1000.0f) {
        snprintf(v, n, "%.2f", ghs / 1000.0f);
        *u = "TH/s";
    } else {
        snprintf(v, n, "%.0f", ghs);
        *u = "GH/s";
    }
}

static void fmt_diff(double d, char *o, size_t n)
{
    const char *s[] = {"", "k", "M", "G", "T", "P", "E"};
    int i = 0;
    while (d >= 1000.0 && i < 6) {
        d /= 1000.0;
        i++;
    }
    snprintf(o, n, i ? "%.1f%s" : "%.0f%s", d, s[i]);
}

static lv_color_t temp_col(float t, float warn, float hot) { return t >= hot ? C_RED : t >= warn ? C_ORANGE : C_GREEN; }

static lv_obj_t *new_page(lv_obj_t *scr)
{
    lv_obj_t *p = lv_obj_create(scr);
    lv_obj_remove_style_all(p);
    lv_obj_set_pos(p, 0, HDR);
    lv_obj_set_size(p, W, PAGE_H);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    return p;
}

// ------------------------------------------------------------------ build
static void build_header(lv_obj_t *scr)
{
    lv_obj_t *h = box(scr, 0, 0, W, HDR, lv_color_hex(0x0a0f18), 0);
    lv_obj_set_style_border_side(h, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(h, C_CYAN, 0);
    lv_obj_set_style_border_opa(h, LV_OPA_40, 0);
    lv_obj_set_style_border_width(h, 1, 0);
    lv_obj_t *logo = rlabel(h, &lv_font_montserrat_14, C_TEXT);
    lv_label_set_text(logo, "#00e5ff AXE##ff2bd6 FORGE#");
    lv_obj_align(logo, LV_ALIGN_LEFT_MID, 6, 0);
    s_status_dot = box(h, 104, 8, 7, 7, C_GREEN, 4);
    s_status = label(h, &lv_font_montserrat_12, C_GREEN, "");
    lv_obj_align(s_status, LV_ALIGN_LEFT_MID, 115, 0);
    s_hdr_right = label(h, &lv_font_montserrat_12, C_DIM, "");
    lv_obj_align(s_hdr_right, LV_ALIGN_RIGHT_MID, -6, 0);
}

static void build_dash(lv_obj_t *p)
{
    s_arc = lv_arc_create(p);
    lv_obj_set_size(s_arc, 132, 132);
    lv_obj_set_pos(s_arc, 4, 4);
    lv_arc_set_bg_angles(s_arc, 135, 45);
    lv_arc_set_range(s_arc, 0, 1000);
    lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_arc, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc, C_TRACK, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc, C_CYAN, LV_PART_INDICATOR);
    // full arc width with centred text: align_to() places a label once, so one
    // sized for "--" drifted right as soon as it showed "12.32"
    s_hash = label(p, &lv_font_montserrat_28, C_TEXT, "--");
    lv_obj_set_width(s_hash, 132);
    lv_obj_set_style_text_align(s_hash, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(s_hash, s_arc, LV_ALIGN_CENTER, 0, -6);
    s_unit = label(p, &lv_font_montserrat_14, C_CYAN, "TH/s");
    lv_obj_set_width(s_unit, 132);
    lv_obj_set_style_text_align(s_unit, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(s_unit, s_arc, LV_ALIGN_CENTER, 0, 18);

    static const char *caps[4] = {"CHIP", "VR", "POWER", "FAN"};
    static const lv_color_t acc[4] = {C_ORANGE, C_VIOLET, C_MAGENTA, C_CYAN};
    lv_obj_t *pn = panel(p, 144, 4, W - 148, 104);
    for (int i = 0; i < 4; i++) {
        box(pn, 0, 8 + i * 24, 3, 12, acc[i], 0);
        s_dash_rows[i] = label(pn, &lv_font_montserrat_12, C_DIM, caps[i]);
        lv_obj_set_pos(s_dash_rows[i], 9, 6 + i * 24);
        s_dash_vals[i] = rlabel(pn, &lv_font_montserrat_14, C_TEXT);
        lv_obj_align(s_dash_vals[i], LV_ALIGN_TOP_RIGHT, -8, 4 + i * 24);
    }
    lv_obj_t *tp = panel(p, 144, 112, W - 148, 28);
    s_tune = rlabel(tp, &lv_font_montserrat_12, C_TEXT);
    lv_obj_set_width(s_tune, W - 164);
    lv_label_set_long_mode(s_tune, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(s_tune, LV_ALIGN_LEFT_MID, 8, 0);
}

static void build_clock(lv_obj_t *p)
{
    s_clk = label(p, &font_clock_110, C_TEXT, "--:--");
    lv_obj_align(s_clk, LV_ALIGN_TOP_MID, 0, 2);
    s_clk_sec = bar(p, 300, C_CYAN);
    lv_bar_set_range(s_clk_sec, 0, 59);
    lv_obj_align(s_clk_sec, LV_ALIGN_TOP_MID, 0, 88);
    s_clk_date = label(p, &lv_font_montserrat_20, C_MAGENTA, "");
    lv_obj_set_pos(s_clk_date, 10, 98);
    s_clk_wx = rlabel(p, &lv_font_montserrat_14, C_TEXT);
    lv_obj_set_width(s_clk_wx, 300);
    lv_label_set_long_mode(s_clk_wx, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(s_clk_wx, 10, 122);
}

static void build_blocks(lv_obj_t *p)
{
    for (int i = 0; i < 4; i++) {
        s_cube[i] = box(p, 6 + i * 78, 4, 72, 56, C_PANEL, 6);
        lv_obj_set_style_bg_grad_dir(s_cube[i], LV_GRAD_DIR_VER, 0);
        s_cube_lbl[i] = label(s_cube[i], &lv_font_montserrat_12, lv_color_hex(0xffffff), "");
        lv_obj_set_style_text_align(s_cube_lbl[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(s_cube_lbl[i]);
    }
    box(p, 81, 2, 2, 60, C_CYAN, 1);  // "now" divider between next and mined
    s_blk_now = rlabel(p, &lv_font_montserrat_14, C_TEXT);
    lv_obj_set_width(s_blk_now, 308);
    lv_label_set_long_mode(s_blk_now, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(s_blk_now, 6, 66);
    lv_obj_t *hp = panel(p, 4, 88, W - 8, 52);
    s_blk_hash = rlabel(hp, &lv_font_montserrat_12, C_DIM);
    lv_obj_set_pos(s_blk_hash, 8, 4);
}

static void build_pool(lv_obj_t *p)
{
    s_pool_tot = rlabel(p, &lv_font_montserrat_14, C_TEXT);
    lv_obj_set_pos(s_pool_tot, 8, 4);
    for (int i = 0; i < AF_POOLS; i++) {
        lv_obj_t *r = panel(p, 4, 26 + i * 29, W - 8, 26);
        s_pool_rows[i] = rlabel(r, &lv_font_montserrat_12, C_TEXT);
        lv_obj_set_width(s_pool_rows[i], W - 24);
        lv_label_set_long_mode(s_pool_rows[i], LV_LABEL_LONG_DOT);
        lv_obj_align(s_pool_rows[i], LV_ALIGN_LEFT_MID, 8, 0);
    }
}

static void build_chips(lv_obj_t *p)
{
    // 8 chips (NerdOctaxe): two rows of half-height cards
    const bool compact = AF_NUM_ASICS > 4;
    const int h = compact ? 66 : 136;
    for (int i = 0; i < AF_NUM_ASICS; i++) {
        lv_obj_t *c = panel(p, 4 + (i % 4) * 79, 4 + (i / 4) * (h + 4), 75, h);
        char cap[8];
        snprintf(cap, sizeof(cap), "ASIC %d", i + 1);
        lv_obj_t *l = label(c, &lv_font_montserrat_12, C_DIM, cap);
        lv_obj_set_pos(l, 7, compact ? 3 : 6);
        s_chip_ghs[i] = label(c, compact ? &lv_font_montserrat_14 : &lv_font_montserrat_20, C_CYAN, "--");
        lv_obj_set_pos(s_chip_ghs[i], 7, compact ? 18 : 26);
        if (!compact) {
            lv_obj_t *u = label(c, &lv_font_montserrat_12, C_DIM, "GH/s");
            lv_obj_set_pos(u, 7, 50);
        }
        s_chip_sub[i] = rlabel(c, &lv_font_montserrat_12, C_TEXT);
        lv_obj_set_pos(s_chip_sub[i], 7, compact ? 35 : 74);
        s_chip_bar[i] = bar(c, 61, C_CYAN);
        lv_obj_align(s_chip_bar[i], LV_ALIGN_BOTTOM_MID, 0, compact ? -4 : -8);
    }
}

static void build_system(lv_obj_t *p)
{
    s_qr = lv_qrcode_create(p, 116, lv_color_black(), lv_color_white());
    lv_obj_set_pos(s_qr, 8, 8);
    lv_obj_set_style_border_color(s_qr, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_border_width(s_qr, 4, 0);
    s_sys = rlabel(p, &lv_font_montserrat_12, C_TEXT);
    lv_obj_set_style_text_line_space(s_sys, 3, 0);
    lv_obj_set_width(s_sys, 176);
    lv_obj_set_pos(s_sys, 138, 6);
}

// ------------------------------------------------------------------ pages
static int kind_of(char c)
{
    const char *f = strchr(KINDS, c);
    return (f && c) ? (int) (f - KINDS) : -1;
}

static void load_order()
{
    s_order_n = 0;
    bool used[NUM_KINDS] = {};
    for (const char *c = settings::get().pages; *c; c++) {
        int k = kind_of(*c);
        if (k >= 0 && !used[k]) {
            used[k] = true;
            s_order[s_order_n++] = k;
        }
    }
    if (!used[K_SYSTEM]) s_order[s_order_n++] = K_SYSTEM;
    int x0 = W / 2 - s_order_n * 6;
    for (int i = 0; i < NUM_KINDS; i++) {
        if (i < s_order_n) {
            lv_obj_clear_flag(s_dots[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(s_dots[i], x0 + i * 12, H - 6);
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
        lv_obj_set_style_bg_color(s_dots[i], i == s_page ? C_CYAN : C_TRACK, 0);
        lv_obj_set_width(s_dots[i], i == s_page ? 12 : 4);
    }
}

static void show_kind(int k)
{
    for (int i = 0; i < s_order_n; i++)
        if (s_order[i] == k) show_page(i);
}

// ------------------------------------------------------------------ refresh
static void refresh_header()
{
    const MinerState &s = g_state;
    lv_color_t c = C_GREEN;
    const char *t = "MINING";
    switch (s.status) {
    case SysStatus::Booting: t = "BOOTING"; c = C_DIM; break;
    case SysStatus::NoWifi: t = "NO WIFI"; c = C_ORANGE; break;
    case SysStatus::SetupAP: t = "SETUP"; c = C_VIOLET; break;
    case SysStatus::Connecting: t = "CONNECTING"; c = C_ORANGE; break;
    case SysStatus::Mining: break;
    case SysStatus::Fault: t = "FAULT"; c = C_RED; break;
    case SysStatus::Overheat: t = "OVERHEAT"; c = C_RED; break;
    }
    lv_label_set_text(s_status, t);
    lv_obj_set_style_text_color(s_status, c, 0);
    lv_obj_set_style_bg_color(s_status_dot, c, 0);
    struct tm tm;
    if (weather::local_time(&tm)) lv_label_set_text_fmt(s_hdr_right, "%s  %02d:%02d", s.ip, tm.tm_hour, tm.tm_min);
    else lv_label_set_text(s_hdr_right, s.ip[0] ? s.ip : "192.168.4.1");

    bool alarm = (s.status == SysStatus::Fault || s.status == SysStatus::Overheat) && s.fault[0];
    if (alarm) {
        lv_obj_clear_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(s_banner_lbl, LV_SYMBOL_WARNING " %s", s.fault);
    } else {
        lv_obj_add_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
    }
}

static void refresh_dash()
{
    const MinerState &s = g_state;
    char v[16];
    const char *u;
    fmt_hash(s.hashrate_ghs, v, sizeof(v), &u);
    lv_label_set_text(s_hash, v);
    lv_label_set_text(s_unit, u);
    float max_ghs = board::limits().max_freq_mhz * (float) asic::SMALL_CORES * AF_NUM_ASICS / 1000.0f;
    lv_arc_set_value(s_arc, (int16_t) (1000.0f * s.hashrate_ghs / max_ghs));
    lv_label_set_text_fmt(s_dash_vals[0], "%.0f\xC2\xB0" "C", s.chip_temp_max);
    lv_obj_set_style_text_color(s_dash_vals[0], temp_col(s.chip_temp_max, CONFIG_AF_MAX_CHIP_TEMP - 6, CONFIG_AF_MAX_CHIP_TEMP), 0);
    if (AF_HAS_VR_SENSOR) lv_label_set_text_fmt(s_dash_vals[1], "%.0f\xC2\xB0" "C", s.vr_temp);
    else lv_label_set_text(s_dash_vals[1], "--");
    float jth = s.hashrate_ghs > 1 ? s.pin / (s.hashrate_ghs / 1000.0f) : 0;
    // one decimal: with two the value ran into the POWER caption on this 320 px screen
    lv_label_set_text_fmt(s_dash_vals[2], "%.1fW #9aa8bd %.0fJ/TH#", s.pin, jth);
    lv_label_set_text_fmt(s_dash_vals[3], "%u%% #9aa8bd %u#", s.fan_pct, s.fan_rpm[0]);
    const char *ph = s.tuner.phase == TunerPhase::Locked ? "#2bff88 LOCKED#"
                     : s.tuner.phase == TunerPhase::Off  ? "#9aa8bd MANUAL#"
                                                         : "#8b5cff TUNING#";
    lv_label_set_text_fmt(s_tune, "%s %u MHz %u.%02uV  %s", ph, s.freq_mhz, s.core_mv / 1000, (s.core_mv % 1000) / 10,
                          s.tuner.note);
}

static void refresh_clock()
{
    static const char *wd[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *mo[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const Settings &cfg = settings::get();
    struct tm t;
    if (weather::local_time(&t)) {
        int h = t.tm_hour;
        if (!cfg.clock_24h) h = h % 12 ? h % 12 : 12;
        lv_label_set_text_fmt(s_clk, "%02d:%02d", h, t.tm_min);  // the big font has digits and ':' only
        lv_bar_set_value(s_clk_sec, t.tm_sec, LV_ANIM_OFF);
        lv_label_set_text_fmt(s_clk_date, "%s %d %s", wd[t.tm_wday], t.tm_mday, mo[t.tm_mon]);
    } else {
        lv_label_set_text(s_clk, "--:--");
        lv_label_set_text(s_clk_date, "Syncing time...");
    }
    const WeatherState &w = g_state.weather;
    char hv[16];
    const char *hu;
    fmt_hash(g_state.hashrate_ghs, hv, sizeof(hv), &hu);
    if (w.valid)
        lv_label_set_text_fmt(s_clk_wx, "#ff9f1c %.0f\xC2\xB0# %s  #9aa8bd %s#  #00e5ff %s %s#", w.temp, weather::describe(w.code),
                              w.city, hv, hu);
    else lv_label_set_text_fmt(s_clk_wx, "#9aa8bd %s#  #00e5ff %s %s#", w.error[0] ? w.error : "no weather", hv, hu);
}

static lv_color_t fee_col(float f)
{
    if (theme::ember()) return lv_color_hex(theme::mempool_fee_rgb(f));
    if (f < 2) return lv_color_hex(0x5b3fd9);
    if (f < 5) return lv_color_hex(0x3d7bff);
    if (f < 10) return lv_color_hex(0x1fbfa0);
    if (f < 20) return lv_color_hex(0x8fd12b);
    if (f < 50) return lv_color_hex(0xffb21c);
    return lv_color_hex(0xff3b5c);
}

static void hash_markup(const char *h, int per_line, char *out, size_t n)
{
    int o = 0, z = 0;
    while (z < 64 && h[z] == '0') z++;
    bool col = false;
    for (int i = 0; i < 64 && o < (int) n - 12; i++) {
        if (i && i % per_line == 0) {
            if (col) out[o++] = '#';
            out[o++] = '\n';
            col = false;
        }
        if (i < z && !col) {
            o += snprintf(out + o, n - o, "#00e5ff ");
            col = true;
        }
        if (i == z && col) {
            out[o++] = '#';
            col = false;
        }
        out[o++] = h[i];
    }
    if (col) out[o++] = '#';
    out[o] = 0;
}

static void refresh_blocks()
{
    const MinerState &s = g_state;
    const MempoolState &m = s.mempool;
    for (int i = 0; i < 4; i++) {
        lv_color_t c;
        if (i == 0) {
            c = m.valid ? fee_col(m.next[0].median_fee) : C_PANEL;
            lv_label_set_text_fmt(s_cube_lbl[i], "next\n~%.0f sat/vB\n%lu tx", m.next[0].median_fee,
                                  (unsigned long) m.next[0].txs);
        } else {
            const MempoolBlock &b = m.mined[i - 1];
            c = m.valid ? (theme::ember() ? fee_col(b.median_fee) : lv_color_mix(fee_col(b.median_fee), lv_color_hex(0x4a3fb0), LV_OPA_50)) : C_PANEL;
            lv_label_set_text_fmt(s_cube_lbl[i], "%lu\n%.10s\n%lu tx", (unsigned long) b.height, b.pool, (unsigned long) b.txs);
        }
        lv_obj_set_style_bg_color(s_cube[i], c, 0);
        lv_obj_set_style_bg_grad_color(s_cube[i], lv_color_darken(c, LV_OPA_40), 0);
    }
    const BlockTemplate &t = s.tmpl;
    if (t.valid) {
        // solo payout check in place of the pool tag when there is a verdict
        const char *pay = t.payout == Payout::PaysYou ? "#2bff88 pays you#" : t.payout == Payout::NotYou ? "#ff3b5c NOT you#" : "";
        if (pay[0])
            lv_label_set_text_fmt(s_blk_now, "#00e5ff " LV_SYMBOL_CHARGE "# Block %lu  #2bff88 %.3f BTC#  %s",
                                  (unsigned long) t.height, t.reward_btc, pay);
        else
            lv_label_set_text_fmt(s_blk_now, "#00e5ff " LV_SYMBOL_CHARGE "# Block %lu  #2bff88 %.3f BTC#  #ff2bd6 %s#",
                                  (unsigned long) t.height, t.reward_btc, t.tag);
    }
    if (s.last_hash[0]) {
        char buf[160];
        hash_markup(s.last_hash, 32, buf, sizeof(buf));
        char d[16];
        fmt_diff(s.last_hash_diff, d, sizeof(d));
        lv_label_set_text_fmt(s_blk_hash, "%s\n#9aa8bd share diff %s#", buf, d);
    }
}

static void refresh_pool()
{
    const MinerState &s = g_state;
    char best[16];
    fmt_diff(s.best_diff, best, sizeof(best));
    uint32_t tot = s.shares_accepted + s.shares_rejected;
    lv_label_set_text_fmt(s_pool_tot, "#2bff88 %lu# / #ff3b5c %lu#  #9aa8bd %.1f%% rej  best# #ff2bd6 %s#  #9aa8bd %s#",
                          (unsigned long) s.shares_accepted, (unsigned long) s.shares_rejected,
                          tot ? 100.0f * s.shares_rejected / tot : 0.0f, best,
                          settings::get().pool_mode == PoolMode::Split ? "split" : "failover");
    static const char *proto[] = {"V1", "TLS", "SV2"};
    for (int i = 0; i < AF_POOLS; i++) {
        const PoolState &p = s.pools[i];
        if (!p.enabled) {
            lv_label_set_text_fmt(s_pool_rows[i], "#9aa8bd Pool %d - not set up#", i + 1);
            continue;
        }
        lv_label_set_text_fmt(s_pool_rows[i], "%s %s #9aa8bd %s# #00e5ff %u%%# #2bff88 %lu#/#ff3b5c %lu#",
                              p.connected ? "#2bff88 " LV_SYMBOL_OK "#" : "#ff9f1c " LV_SYMBOL_REFRESH "#", p.host,
                              proto[p.proto < 3 ? p.proto : 0], p.work_pct, (unsigned long) p.accepted,
                              (unsigned long) p.rejected);
    }
}

static void refresh_chips()
{
    const MinerState &s = g_state;
    float per = s.expected_ghs / AF_NUM_ASICS;
    for (int i = 0; i < AF_NUM_ASICS; i++) {
        lv_label_set_text_fmt(s_chip_ghs[i], "%.0f", s.chip_ghs[i]);
        float tot = s.chip_ghs[i] + s.chip_err_ghs[i];
        // compact cards (8 chips) have room for one line only
        lv_label_set_text_fmt(s_chip_sub[i], AF_NUM_ASICS > 4 ? "%s%.0f\xC2\xB0 #9aa8bd %.1f%%#" : "%s%.0f\xC2\xB0" "C\n#9aa8bd err %.1f%%#",
                              s.chip_temp_own[i] ? "" : "~",
                              s.chip_temp[i], tot > 0 ? 100.0f * s.chip_err_ghs[i] / tot : 0.0f);
        lv_bar_set_value(s_chip_bar[i], per > 0 ? (int) (100 * s.chip_ghs[i] / per) : 0, LV_ANIM_ON);
    }
}

static void refresh_system()
{
    const MinerState &s = g_state;
    char qr[96];
    if (s.ap_ssid[0] && !s.ip[0]) {
        snprintf(qr, sizeof(qr), "WIFI:T:nopass;S:%s;;", s.ap_ssid);
        lv_label_set_text_fmt(s_sys, "#00e5ff SETUP#\n1. Join Wi-Fi\n#00e5ff %s#\n2. Open\n#ff2bd6 http://192.168.4.1#\n3. Enter Wi-Fi + pool",
                              s.ap_ssid);
    } else {
        snprintf(qr, sizeof(qr), "http://%s/", s.ip);
        lv_label_set_text_fmt(s_sys, "#00e5ff SYSTEM#\n#9aa8bd Web UI#\nhttp://%s\n#9aa8bd Wi-Fi# %s\n#9aa8bd FW# AxeForge %s\n#9aa8bd Chips# %d x %s",
                              s.ip, s.ssid, esp_app_get_description()->version, s.chips_detected, asic::MODEL);
    }
    if (strcmp(qr, s_qr_text)) {
        strcpy(s_qr_text, qr);
        lv_qrcode_update(s_qr, s_qr_text, strlen(s_qr_text));
    }
}

static void refresh_cb(lv_timer_t *)
{
    if (s_req_reload) {
        s_req_reload = false;
        int k = s_order[s_page];
        load_order();
        s_page = 0;
        show_kind(k);
        show_page(s_page);
    }
    int req = s_req_page;
    if (req) {
        s_req_page = 0;
        if (req >= 100) show_kind(req - 100);
        else show_page(s_page + req);
    }
    refresh_header();
    switch (s_order[s_page]) {
    case K_DASH: refresh_dash(); break;
    case K_CLOCK: refresh_clock(); break;
    case K_BLOCKS: refresh_blocks(); break;
    case K_POOL: refresh_pool(); break;
    case K_CHIPS: refresh_chips(); break;
    case K_SYSTEM: refresh_system(); break;
    }
    uint8_t rot = settings::get().page_rotate_s;
    uint32_t now = lv_tick_get();
    if (rot && now - s_last_input_ms > 60000 && now - s_page_since_ms > rot * 1000u && !g_state.ap_ssid[0]) {
        int next = (s_page + 1) % s_order_n;
        if (s_order[next] == K_SYSTEM) next = (next + 1) % s_order_n;
        show_page(next);
    }
}

// ------------------------------------------------------------------ public
namespace ui_small {

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
    build_dash(s_pages[K_DASH]);
    build_clock(s_pages[K_CLOCK]);
    build_blocks(s_pages[K_BLOCKS]);
    build_pool(s_pages[K_POOL]);
    build_chips(s_pages[K_CHIPS]);
    build_system(s_pages[K_SYSTEM]);
    for (int i = 0; i < NUM_KINDS; i++) s_dots[i] = box(scr, 0, H - 6, 4, 4, C_TRACK, 2);

    s_banner = box(scr, 0, HDR, W, 22, C_RED, 0);
    lv_obj_set_style_bg_opa(s_banner, LV_OPA_90, 0);
    s_banner_lbl = label(s_banner, &lv_font_montserrat_12, lv_color_hex(0xffffff), "");
    lv_obj_center(s_banner_lbl);
    lv_obj_add_flag(s_banner, LV_OBJ_FLAG_HIDDEN);

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

void on_long_press(int index)
{
    if (index == 0) {
        s_backlight = !s_backlight;
        display::set_brightness(s_backlight ? settings::get().brightness : 0);
    } else {
        tuner::restart();
    }
}

void request_page(int delta) { s_req_page = delta; }
void request_kind(char kind)
{
    int k = kind_of(kind);
    if (k >= 0) s_req_page = 100 + k;
}
void reload_pages() { s_req_reload = true; }
char current_kind() { return s_order_n ? KINDS[s_order[s_page]] : 'd'; }

} // namespace ui_small
