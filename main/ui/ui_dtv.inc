// DTV theme on the 3.5in 480x320 screen: the same design as the 320x170 one
// (ui_small_dtv.inc), laid out for the bigger panel. Included by ui.cpp, which
// owns page order, buttons, the header state and the custom pages.

#define B_RAIL 36
#define B_X0 46                     // content left
#define B_Y0 (HEADER_H + 4)          // content top; the header keeps its height so
                                     // the custom pages' grid still fits under it
#define B_PW (LCD_H_RES - B_X0)      // 434
#define B_PH (LCD_V_RES - B_Y0)      // 282

#include "dtv_shapes.inc"

static lv_obj_t *b_rail_name, *b_rail_idx, *b_splash, *b_splash_bar;
static int b_splash_pct = 0;
// dashboard
static lv_obj_t *b_hash, *b_unit, *b_sub, *b_plate[4], *b_tune, *b_stack[5], *b_chart;
static lv_chart_series_t *b_series;
// clock
static lv_obj_t *b_digit[4], *b_ticks, *b_date, *b_wx, *b_city, *b_day[3], *b_day_hi[3], *b_day_lo[3];
// blocks
static lv_obj_t *b_blk[5], *b_blk_l1[5], *b_blk_l2[5], *b_blk_l3[5], *b_now_h, *b_now_info, *b_hash_lbl, *b_hash_diff;
// pool
static lv_obj_t *b_ptot, *b_pmode, *b_tix[AF_POOLS], *b_tix_proto[AF_POOLS], *b_tix_host[AF_POOLS], *b_tix_pct[AF_POOLS],
    *b_tix_info[AF_POOLS];
// chips
static lv_obj_t *b_chip[AF_NUM_ASICS], *b_chip_ghs[AF_NUM_ASICS], *b_chip_t[AF_NUM_ASICS], *b_chip_err[AF_NUM_ASICS],
    *b_chip_bar[AF_NUM_ASICS], *b_td_phase, *b_td_best, *b_td_trial, *b_td_note;
// system
static lv_obj_t *b_qr, *b_key[7], *b_val[7];

static const char *const B_NAMES[NUM_KINDS] = {"H\nA\nS\nH", "T\nI\nM\nE", "B\nL\nO\nC\nK", "P\nO\nO\nL", "C\nH\nI\nP\nS",
                                               "S\nY\nS", "M\nY\n1", "M\nY\n2"};

static lv_obj_t *b_page(lv_obj_t *scr)
{
    lv_obj_t *p = lv_obj_create(scr);
    lv_obj_remove_style_all(p);
    lv_obj_set_pos(p, B_X0, B_Y0);
    lv_obj_set_size(p, B_PW, B_PH);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    return p;
}

// ------------------------------------------------------------------ build
static void b_build_frame(lv_obj_t *scr)
{
    lv_obj_t *rail = d_box(scr, 0, 0, B_RAIL, LCD_V_RES, lv_color_hex(D_OR), 0);
    b_rail_name = label(rail, &lv_font_montserrat_20, lv_color_hex(D_INK), "");
    lv_obj_set_width(b_rail_name, B_RAIL);
    lv_obj_set_style_text_align(b_rail_name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(b_rail_name, -4, 0);
    lv_obj_align(b_rail_name, LV_ALIGN_LEFT_MID, 0, 0);
    b_rail_idx = label(rail, &lv_font_montserrat_20, lv_color_hex(D_INK), "01");
    lv_obj_set_width(b_rail_idx, B_RAIL);
    lv_obj_set_style_text_align(b_rail_idx, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(b_rail_idx, LV_ALIGN_BOTTOM_MID, 0, -6);
    shape(scr, Sh::Diamond, B_RAIL - 6, 6, 12, 12, D_INK);

    lv_obj_t *logo = lv_img_create(scr);
    lv_img_set_src(logo, &dtv_logo_26);
    lv_obj_set_pos(logo, B_X0, 4);

    s_status_pill = lv_obj_create(scr);
    lv_obj_remove_style_all(s_status_pill);
    lv_obj_set_size(s_status_pill, 120, 22);
    lv_obj_set_pos(s_status_pill, B_X0 + 86, 6);
    lv_obj_set_style_radius(s_status_pill, 0, 0);  // DTV: square pills
    lv_obj_set_style_bg_opa(s_status_pill, LV_OPA_20, 0);
    lv_obj_set_style_border_width(s_status_pill, 1, 0);
    s_status_dot = d_box(s_status_pill, 8, 7, 7, 7, C_GREEN, 0);
    s_status_lbl = label(s_status_pill, &lv_font_montserrat_12, C_GREEN, "BOOTING");
    lv_obj_align(s_status_lbl, LV_ALIGN_LEFT_MID, 22, 0);
    s_uptime_lbl = label(scr, &lv_font_montserrat_12, lv_color_hex(D_DIM), "");
    lv_obj_align(s_uptime_lbl, LV_ALIGN_TOP_RIGHT, -8, 10);
    s_ip_lbl = label(scr, &lv_font_montserrat_12, lv_color_hex(D_TXT), "");
    lv_obj_align(s_ip_lbl, LV_ALIGN_TOP_RIGHT, -78, 10);
    d_box(scr, B_X0, HEADER_H, LCD_H_RES - B_X0 - 6, 1, lv_color_hex(D_RULE), 0);
}

static void b_build_dash(lv_obj_t *p)
{
    dlabel(p, &lv_font_montserrat_14, D_OR, 2, 0, "HASHRATE");
    b_hash = dlabel(p, &lv_font_montserrat_48, D_TXT, 0, 18, "--");
    b_unit = dlabel(p, &lv_font_montserrat_28, D_OR, 140, 36, "TH/s");
    b_sub = dlabel(p, &lv_font_montserrat_14, D_DIM, 2, 74);
    static const char *caps[4] = {"CHIP", "VR", "POWER", "FAN"};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *c = cham(p, i * 80, 98, 74, 60);
        dlabel(c, &lv_font_montserrat_14, D_OR, 9, 6, caps[i]);
        b_plate[i] = dlabel(c, &lv_font_montserrat_20, D_TXT, 9, 28, "--");  // "95.8W" fits 74 px at 20
    }
    b_tune = dlabel(p, &lv_font_montserrat_14, D_DIM, 2, 168);
    lv_obj_set_width(b_tune, 312);
    lv_label_set_long_mode(b_tune, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_t *cp = cham(p, 0, 192, 314, 86);
    dlabel(cp, &lv_font_montserrat_12, D_OR, 9, 3, "LAST HOUR");
    b_chart = lv_chart_create(cp);
    lv_obj_set_size(b_chart, 296, 58);
    lv_obj_set_pos(b_chart, 8, 22);
    lv_obj_set_style_bg_opa(b_chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(b_chart, 0, 0);
    lv_obj_set_style_pad_all(b_chart, 0, 0);
    lv_obj_set_style_line_color(b_chart, lv_color_hex(D_RULE), LV_PART_MAIN);
    lv_chart_set_div_line_count(b_chart, 3, 0);
    lv_chart_set_type(b_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(b_chart, AF_HISTORY_LEN);
    lv_obj_set_style_size(b_chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(b_chart, 2, LV_PART_ITEMS);
    b_series = lv_chart_add_series(b_chart, lv_color_hex(D_OR), LV_CHART_AXIS_PRIMARY_Y);
    // five crates, bottom first so each sits on the one below; the coin on top
    const int cx = 376, w = 92, depth = 14, b = (w * 28 + 50) / 100, oh = 2 * b + depth + 1, bottom = B_PH - 4;
    for (int k = 0; k < 5; k++) b_stack[k] = crate(p, Sh::Crate, cx, bottom - oh - k * 30, w, depth);
    coin(p, cx, bottom - oh - 4 * 30 - 16, 20);
}

static void b_build_clock(lv_obj_t *p)
{
    static const int tx[4] = {0, 82, 186, 268};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *t = cham(p, tx[i], 4, 76, 124);
        shp(t)->n = 1;
        b_digit[i] = label(t, &font_clock_110, lv_color_hex(D_TXT), "-");
        lv_obj_set_width(b_digit[i], 76);
        lv_obj_set_style_text_align(b_digit[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(b_digit[i], LV_ALIGN_CENTER, 0, 2);
    }
    shape(p, Sh::Diamond, 166, 40, 13, 13, D_OR);
    shape(p, Sh::Diamond, 166, 80, 13, 13, D_OR);
    coin(p, 394, 66, 30);
    b_ticks = shape(p, Sh::Ticks, 0, 140, B_PW - 4, 10, D_OR);
    b_date = dlabel(p, &lv_font_montserrat_28, D_OR, 0, 160);
    b_wx = dlabel(p, &lv_font_montserrat_20, D_TXT, 0, 202);
    b_city = dlabel(p, &lv_font_montserrat_14, D_DIM, 0, 230);
    for (int i = 0; i < 3; i++) {
        lv_obj_t *c = cham(p, 222 + i * 70, 162, 64, 114);
        b_day[i] = clabel(c, &lv_font_montserrat_14, D_DIM, 32, 6, 60);
        b_day_hi[i] = clabel(c, &lv_font_montserrat_28, D_TXT, 32, 30, 60);
        b_day_lo[i] = clabel(c, &lv_font_montserrat_14, D_DIM, 32, 66, 60);
        lv_obj_t *dot = shape(c, Sh::Diamond, 26, 90, 12, 12, D_OR);
        LV_UNUSED(dot);
    }
}

static void b_build_blocks(lv_obj_t *p)
{
    static const int cx[5] = {40, 126, 212, 298, 384};
    dlabel(p, &lv_font_montserrat_14, D_DIM, 4, 0, "coming");
    dlabel(p, &lv_font_montserrat_14, D_OR, 236, 0, "mined");
    for (int y = 22; y < 150; y += 8) d_box(p, 168, y, 2, 4, lv_color_hex(D_OR), 0);
    for (int i = 0; i < 5; i++) {
        b_blk[i] = crate(p, i < 2 ? Sh::Wire : Sh::Crate, cx[i], 32, 72, 34);
        if (i >= 2) set_crate(b_blk[i], true, i == 2 ? D_HOT : D_LIT);
        b_blk_l1[i] = clabel(p, i < 2 ? &lv_font_montserrat_14 : &lv_font_montserrat_20, i < 2 ? D_DIM : D_TXT, cx[i], i < 2 ? 116 : 112, 84);
        b_blk_l2[i] = clabel(p, &lv_font_montserrat_14, D_DIM, cx[i], 136, 84);
        b_blk_l3[i] = clabel(p, &lv_font_montserrat_12, D_DIM, cx[i], 154, 84);
    }
    coin(p, cx[2], 16, 13);
    lv_obj_t *n = cham(p, 0, 176, B_PW - 2, 58);
    dlabel(n, &lv_font_montserrat_14, D_OR, 10, 4, "NOW MINING");
    b_now_h = dlabel(n, &lv_font_montserrat_28, D_TXT, 10, 22, "--");
    b_now_info = dlabel(n, &lv_font_montserrat_14, D_TXT, 150, 30);
    lv_obj_set_width(b_now_info, B_PW - 166);
    lv_label_set_long_mode(b_now_info, LV_LABEL_LONG_DOT);
    lv_obj_t *h = cham(p, 0, 238, B_PW - 2, 42);
    dlabel(h, &lv_font_montserrat_12, D_OR, 10, 3, "LIVE SHARE HASH");
    b_hash_diff = rlabel_at(h, &lv_font_montserrat_12, D_DIM, B_PW - 14, 3, 200);
    b_hash_lbl = dlabel(h, &lv_font_montserrat_12, D_DIM, 10, 21);
    lv_obj_set_size(b_hash_lbl, B_PW - 24, 15);  // one line: the rest is cut, not wrapped
    lv_label_set_long_mode(b_hash_lbl, LV_LABEL_LONG_CLIP);
}

static void b_build_pool(lv_obj_t *p)
{
    b_ptot = dlabel(p, &lv_font_montserrat_28, D_TXT, 0, 0, "--");
    dlabel(p, &lv_font_montserrat_14, D_DIM, 0, 34, "accepted / rejected / best");
    b_pmode = rlabel_at(p, &lv_font_montserrat_14, D_OR, B_PW - 6, 34, 120);
    for (int i = 0; i < AF_POOLS; i++) {
        const int y = 58 + i * 56;
        b_tix[i] = shape(p, Sh::Ticket, 0, y, B_PW - 8, 50, D_RULE);
        shp(b_tix[i])->depth = 56;
        b_tix_proto[i] = clabel(p, &lv_font_montserrat_14, D_DIM, 28, y + 16, 54);
        b_tix_host[i] = dlabel(p, &lv_font_montserrat_20, D_TXT, 68, y + 4);
        lv_obj_set_width(b_tix_host[i], 240);
        lv_label_set_long_mode(b_tix_host[i], LV_LABEL_LONG_DOT);
        b_tix_pct[i] = rlabel_at(p, &lv_font_montserrat_20, D_OR, B_PW - 22, y + 4, 70);
        b_tix_info[i] = dlabel(p, &lv_font_montserrat_12, D_DIM, 68, y + 30);
        lv_obj_set_width(b_tix_info[i], B_PW - 96);
        lv_label_set_long_mode(b_tix_info[i], LV_LABEL_LONG_DOT);
    }
}

static void b_build_chips(lv_obj_t *p)
{
    const int n = AF_NUM_ASICS, per = n < 4 ? n : 4, rows = (n + 3) / 4, cw = B_PW / per;
    for (int i = 0; i < n; i++) {
        const int cx = (i % per) * cw + cw / 2;
        if (rows == 1) {
            const int w = cw / 2 < 96 ? cw / 2 : 96, b = (w * 28 + 50) / 100, bot = 52 + 2 * b + 25;
            b_chip_ghs[i] = clabel(p, &lv_font_montserrat_28, D_TXT, cx, 0, cw);
            lv_obj_t *u = clabel(p, &lv_font_montserrat_12, D_DIM, cx, 32, cw);
            lv_label_set_text(u, "GH/s");
            b_chip[i] = crate(p, Sh::Crate, cx, 52, w, 24);
            b_chip_t[i] = clabel(p, &lv_font_montserrat_20, D_TXT, cx, bot + 6, cw);
            lv_obj_t *cap = clabel(p, &lv_font_montserrat_14, D_OR, cx, bot + 30, cw);
            lv_label_set_text_fmt(cap, "ASIC %d", i + 1);
            b_chip_err[i] = clabel(p, &lv_font_montserrat_12, D_DIM, cx, bot + 48, cw);
            b_chip_bar[i] = thin_bar(p, lv_color_hex(D_OR));
            lv_obj_set_width(b_chip_bar[i], cw - 20);
            lv_obj_set_pos(b_chip_bar[i], cx - (cw - 20) / 2, 186);
        } else {
            const int y0 = (i / per) * 96;
            b_chip_ghs[i] = clabel(p, &lv_font_montserrat_20, D_TXT, cx, y0, cw);
            b_chip[i] = crate(p, Sh::Crate, cx, y0 + 24, 40, 12);
            b_chip_t[i] = clabel(p, &lv_font_montserrat_14, D_TXT, cx, y0 + 62, cw);
            b_chip_err[i] = nullptr;
            b_chip_bar[i] = thin_bar(p, lv_color_hex(D_OR));
            lv_obj_set_width(b_chip_bar[i], cw - 20);
            lv_obj_set_pos(b_chip_bar[i], cx - (cw - 20) / 2, y0 + 84);
        }
        lv_obj_set_style_bg_color(b_chip_bar[i], lv_color_hex(0x161616), LV_PART_MAIN);
        lv_obj_set_style_radius(b_chip_bar[i], 0, LV_PART_MAIN);
        lv_obj_set_style_radius(b_chip_bar[i], 0, LV_PART_INDICATOR);
    }
    lv_obj_t *t = cham(p, 0, 198, B_PW - 2, 82);
    dlabel(t, &lv_font_montserrat_14, D_OR, 10, 4, "AUTO-TUNE");
    dlabel(t, &lv_font_montserrat_12, D_DIM, 100, 6, "hold BTN2 to re-tune");
    b_td_phase = dlabel(t, &lv_font_montserrat_20, D_TXT, 10, 22);
    b_td_best = dlabel(t, &lv_font_montserrat_14, D_TXT, 10, 48);
    b_td_trial = rlabel_at(t, &lv_font_montserrat_14, D_DIM, B_PW - 14, 24, 220);
    b_td_note = dlabel(t, &lv_font_montserrat_12, D_DIM, 10, 66);
    lv_obj_set_width(b_td_note, B_PW - 24);
    lv_label_set_long_mode(b_td_note, LV_LABEL_LONG_DOT);
}

static void b_build_system(lv_obj_t *p)
{
    lv_obj_t *f = cham(p, 0, 0, 200, 200);
    shp(f)->fill = D_OR;
    shp(f)->opa = LV_OPA_COVER;
    b_qr = lv_qrcode_create(p, 176, lv_color_black(), lv_color_white());
    lv_obj_set_pos(b_qr, 12, 12);
    lv_obj_t *c = clabel(p, &lv_font_montserrat_14, D_OR, 100, 210, 200);
    lv_label_set_text(c, "SCAN TO OPEN");
    lv_obj_set_style_text_letter_space(c, 2, 0);
    for (int i = 0; i < 7; i++) {
        b_key[i] = dlabel(p, &lv_font_montserrat_14, D_DIM, 214, 2 + i * 23);
        b_val[i] = rlabel_at(p, &lv_font_montserrat_14, D_TXT, B_PW - 6, 2 + i * 23, 150);
    }
    lv_obj_t *plate = d_box(p, 214, 172, B_PW - 220, 28, lv_color_hex(D_OR), 0);
    lv_obj_t *pl = label(plate, &lv_font_montserrat_14, lv_color_hex(D_INK), "FIRST SUPPORTER");
    lv_obj_set_style_text_letter_space(pl, 2, 0);
    lv_obj_center(pl);
    lv_obj_t *logo = lv_img_create(p);
    lv_img_set_src(logo, &dtv_logo_26);
    lv_obj_set_pos(logo, 214, 214);
}

static void b_splash_cb(lv_timer_t *t)
{
    b_splash_pct += 4;
    if (b_splash && b_splash_bar) lv_obj_set_width(b_splash_bar, b_splash_pct * 240 / 100);
    if (b_splash_pct >= 100) {
        if (b_splash) lv_obj_del(b_splash);
        b_splash = b_splash_bar = nullptr;
        lv_timer_del(t);
    }
}

static void b_build_splash(lv_obj_t *scr)
{
    b_splash = d_box(scr, 0, 0, LCD_H_RES, LCD_V_RES, lv_color_hex(0x000000), 0);
    lv_obj_t *logo = lv_img_create(b_splash);
    lv_img_set_src(logo, &dtv_logo_56);
    lv_img_set_antialias(logo, true);
    lv_img_set_zoom(logo, 420);  // 1.64x of the 157x56 logo
    lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 86);
    lv_obj_t *fs = label(b_splash, &lv_font_montserrat_14, lv_color_hex(D_OR), "FIRST SUPPORTER");
    lv_obj_set_style_text_letter_space(fs, 4, 0);
    lv_obj_align(fs, LV_ALIGN_TOP_MID, 0, 186);
    lv_obj_t *track = d_box(b_splash, (LCD_H_RES - 240) / 2, 218, 240, 6, lv_color_hex(0x161616), 3);
    b_splash_bar = d_box(track, 0, 0, 0, 6, lv_color_hex(D_OR), 3);
    lv_obj_t *by = label(b_splash, &lv_font_montserrat_14, lv_color_hex(D_DIM), "");
    lv_label_set_text_fmt(by, "powered by AxeForge %s", esp_app_get_description()->version);
    lv_obj_align(by, LV_ALIGN_TOP_MID, 0, 246);
    b_splash_pct = 0;
    lv_timer_create(b_splash_cb, 100, nullptr);
}

static void b_build(lv_obj_t *scr)
{
    s_nshp = 0;
    b_build_frame(scr);
    for (int i = 0; i < NUM_KINDS; i++) s_pages[i] = b_page(scr);
    b_build_dash(s_pages[K_DASH]);
    b_build_clock(s_pages[K_CLOCK]);
    b_build_blocks(s_pages[K_BLOCKS]);
    b_build_pool(s_pages[K_POOL]);
    b_build_chips(s_pages[K_CHIPS]);
    b_build_system(s_pages[K_SYSTEM]);
    // the custom pages are a grid laid out for the full width under the header:
    // they keep that place and cover the rail below the header while shown
    static const int custom[2] = {K_CUSTOM1, K_CUSTOM2};
    for (int k : custom) {
        lv_obj_del(s_pages[k]);
        s_pages[k] = new_page(scr);
        lv_obj_set_style_bg_color(s_pages[k], lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(s_pages[k], LV_OPA_COVER, 0);
    }
    s_custom_root[0] = s_pages[K_CUSTOM1];
    s_custom_root[1] = s_pages[K_CUSTOM2];
    build_custom(0);
    build_custom(1);
    // page dots exist (load_order places them) but out of sight: the rail numbers pages
    lv_obj_t *hide = d_box(scr, 0, 0, 1, 1, lv_color_hex(0), 0);
    lv_obj_add_flag(hide, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < NUM_KINDS; i++) s_dots[i] = d_box(hide, 0, 0, 6, 6, C_TRACK, 0);
    s_banner = d_box(scr, B_RAIL, HEADER_H + 1, LCD_H_RES - B_RAIL, 30, C_RED, 0);
    lv_obj_set_style_bg_opa(s_banner, LV_OPA_90, 0);
    s_banner_lbl = label(s_banner, &lv_font_montserrat_14, lv_color_hex(0xffffff), "");
    lv_obj_center(s_banner_lbl);
    lv_obj_add_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
    b_build_splash(scr);
}

static void b_show_page()
{
    lv_label_set_text(b_rail_name, B_NAMES[s_order[s_page]]);
    lv_label_set_text_fmt(b_rail_idx, "%02d", s_page + 1);
}

// ------------------------------------------------------------------ refresh
static void b_refresh_dash()
{
    const MinerState &s = g_state;
    char v[16];
    const char *u;
    fmt_hash(s.hashrate_ghs, v, sizeof(v), &u);
    lv_label_set_text(b_hash, v);
    lv_label_set_text(b_unit, u);
    lv_obj_align_to(b_unit, b_hash, LV_ALIGN_OUT_RIGHT_BOTTOM, 8, -10);
    float pct = s.expected_ghs > 1 ? 100.0f * s.hashrate_ghs / s.expected_ghs : 0;
    char ev[16];
    const char *eu;
    fmt_hash(s.expected_ghs, ev, sizeof(ev), &eu);
    float jth = s.hashrate_ghs > 1 ? s.pin / (s.hashrate_ghs / 1000.0f) : 0;
    lv_label_set_text_fmt(b_sub, "%.0f%% of %s %s    err %.2f%%    %.1f J/TH", pct, ev, eu, s.error_pct, jth);
    int lit = (int) ((pct + 10) / 20);
    if (lit > 5) lit = 5;
    if (lit < 0) lit = 0;
    for (int k = 0; k < 5; k++) set_crate(b_stack[k], k < lit, k == lit - 1 ? D_HOT : D_LIT);
    lv_label_set_text_fmt(b_plate[0], "%.0f\xC2\xB0", s.chip_temp_max);
    lv_obj_set_style_text_color(b_plate[0], lv_color_hex(s.chip_temp_max >= CONFIG_AF_MAX_CHIP_TEMP ? D_RED
                                                         : s.chip_temp_max >= CONFIG_AF_MAX_CHIP_TEMP - 6 ? D_LIT
                                                                                                          : D_TXT), 0);
    if (AF_HAS_VR_SENSOR) lv_label_set_text_fmt(b_plate[1], "%.0f\xC2\xB0", s.vr_temp);
    else lv_label_set_text(b_plate[1], "--");
    lv_label_set_text_fmt(b_plate[2], "%.1fW", s.pin);
    lv_label_set_text_fmt(b_plate[3], "%u%%", s.fan_pct);
    lv_label_set_text_fmt(b_tune, "#ff8000 %s#   %u MHz   %u.%02u V   %s", phase_name(s.tuner.phase), s.freq_mhz,
                          s.core_mv / 1000, (s.core_mv % 1000) / 10, s.tuner.note);
    float vmax = 1;
    for (int i = 0; i < AF_HISTORY_LEN; i++)
        if (s.history[i] > vmax) vmax = s.history[i];
    lv_chart_set_range(b_chart, LV_CHART_AXIS_PRIMARY_Y, 0, (lv_coord_t) (vmax * 1.1f));
    for (int i = 0; i < AF_HISTORY_LEN; i++) {
        float h = s.history[(s.history_head + i) % AF_HISTORY_LEN];
        b_series->y_points[i] = h > 0 ? (lv_coord_t) h : LV_CHART_POINT_NONE;
    }
    lv_chart_refresh(b_chart);
}

static void b_refresh_clock()
{
    static const char *wd[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char *mo[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    struct tm t;
    Shape *tk = shp(b_ticks);
    bool have = weather::local_time(&t);
    if (have) {
        int h = t.tm_hour;
        if (!settings::get().clock_24h) h = h % 12 ? h % 12 : 12;
        const int dg[4] = {h / 10, h % 10, t.tm_min / 10, t.tm_min % 10};
        for (int i = 0; i < 4; i++) lv_label_set_text_fmt(b_digit[i], "%d", dg[i]);
        if (tk && tk->n != (t.tm_sec + 1) / 2) {
            tk->n = (uint8_t) ((t.tm_sec + 1) / 2);
            lv_obj_invalidate(b_ticks);
        }
        lv_label_set_text_fmt(b_date, "%s %d %s", wd[t.tm_wday], t.tm_mday, mo[t.tm_mon]);
    } else {
        for (int i = 0; i < 4; i++) lv_label_set_text(b_digit[i], "");
        lv_label_set_text(b_date, "SYNCING TIME");
    }
    const WeatherState &w = g_state.weather;
    if (w.valid) {
        lv_label_set_text_fmt(b_wx, "%.0f\xC2\xB0  %s", w.temp, weather::describe(w.code));
        lv_label_set_text_fmt(b_city, "%s   %u%% hum", w.city, w.humidity);
    } else {
        lv_label_set_text(b_wx, "");
        lv_label_set_text(b_city, w.error[0] ? w.error : "no weather yet");
    }
    for (int i = 0; i < 3; i++) {
        const WeatherDay &d = w.days[i];
        lv_label_set_text(b_day[i], i == 0 ? "TODAY" : have ? wd[(t.tm_wday + i) % 7] : "--");
        if (w.valid && d.code >= 0) {
            lv_label_set_text_fmt(b_day_hi[i], "%.0f\xC2\xB0", d.tmax);
            lv_label_set_text_fmt(b_day_lo[i], "%.0f\xC2\xB0", d.tmin);
        } else {
            lv_label_set_text(b_day_hi[i], "--");
            lv_label_set_text(b_day_lo[i], "");
        }
    }
}

static void b_refresh_blocks()
{
    const MinerState &s = g_state;
    const MempoolState &m = s.mempool;
    time_t now_t = time(nullptr);
    for (int i = 0; i < 5; i++) {
        if (!m.valid) {
            lv_label_set_text(b_blk_l1[i], "--");
            lv_label_set_text(b_blk_l2[i], "");
            lv_label_set_text(b_blk_l3[i], i == 0 ? (m.error[0] ? m.error : "loading") : "");
            continue;
        }
        if (i < 2) {  // the block after next on the left, the next one by the divider
            const MempoolNext &nb = m.next[1 - i];
            lv_label_set_text_fmt(b_blk_l1[i], "~%.0f sat/vB", nb.median_fee);
            lv_label_set_text(b_blk_l2[i], i == 1 ? "next" : "after");
            lv_label_set_text_fmt(b_blk_l3[i], "%lu txs", (unsigned long) nb.txs);
        } else {
            const MempoolBlock &b = m.mined[i - 2];
            lv_label_set_text_fmt(b_blk_l1[i], "%lu", (unsigned long) b.height);
            if (b.time && now_t > 1600000000) lv_label_set_text_fmt(b_blk_l2[i], "%ld min ago", (long) ((now_t - b.time) / 60));
            else lv_label_set_text_fmt(b_blk_l2[i], "%lu txs", (unsigned long) b.txs);
            lv_label_set_text(b_blk_l3[i], b.pool);
        }
    }
    const BlockTemplate &t = s.tmpl;
    if (t.valid) {
        lv_label_set_text_fmt(b_now_h, "%lu", (unsigned long) t.height);
        const char *pay = t.payout == Payout::PaysYou ? "#00cc88 pays you#" : t.payout == Payout::NotYou ? "#f0434f NOT you#" : t.tag;
        lv_label_set_text_fmt(b_now_info, "#ff8000 %.3f BTC#   %s", t.reward_btc, pay);
    }
    if (s.last_hash[0]) {
        int z = 0;
        while (z < 64 && s.last_hash[z] == '0') z++;
        lv_label_set_text_fmt(b_hash_lbl, "#ff8000 %.*s#%s", z, s.last_hash, s.last_hash + z);
        char d[16], best[16];
        fmt_diff(s.last_hash_diff, d, sizeof(d));
        fmt_diff(s.best_diff, best, sizeof(best));
        lv_label_set_text_fmt(b_hash_diff, "diff %s   best %s", d, best);
    }
}

static void b_refresh_pool()
{
    const MinerState &s = g_state;
    char best[16];
    fmt_diff(s.best_diff, best, sizeof(best));
    lv_label_set_text_fmt(b_ptot, "%lu #ff8000 /# %lu #ff8000 /# %s", (unsigned long) s.shares_accepted,
                          (unsigned long) s.shares_rejected, best);
    lv_label_set_text(b_pmode, settings::get().pool_mode == PoolMode::Split ? "SPLIT" : "FAILOVER");
    static const char *proto[] = {"SV1", "TLS", "SV2"};
    for (int i = 0; i < AF_POOLS; i++) {
        const PoolState &p = s.pools[i];
        const bool live = p.enabled && p.connected && p.work_pct > 0;
        Shape *t = shp(b_tix[i]);
        if (t && t->c1 != (live ? (uint32_t) D_OR : (uint32_t) D_RULE)) {
            t->c1 = live ? D_OR : D_RULE;
            lv_obj_invalidate(b_tix[i]);
        }
        lv_obj_set_style_text_color(b_tix_proto[i], lv_color_hex(live ? D_OR : D_DIM), 0);
        lv_obj_set_style_text_color(b_tix_host[i], lv_color_hex(p.enabled ? D_TXT : D_DIM), 0);
        if (!p.enabled) {
            lv_label_set_text_fmt(b_tix_proto[i], "%d", i + 1);
            lv_label_set_text(b_tix_host[i], "not set up");
            lv_label_set_text(b_tix_pct[i], "");
            lv_label_set_text(b_tix_info[i], "add it in the web UI > Pools");
            continue;
        }
        lv_label_set_text(b_tix_proto[i], proto[p.proto < 3 ? p.proto : 0]);
        lv_label_set_text(b_tix_host[i], p.host);
        lv_label_set_text_fmt(b_tix_pct[i], "%u%%", p.work_pct);
        char pb[16];
        fmt_diff(p.best, pb, sizeof(pb));
        lv_label_set_text_fmt(b_tix_info[i], "#00cc88 %lu# / #f0434f %lu#   diff %lu   best %s   %lu ms   %s",
                              (unsigned long) p.accepted, (unsigned long) p.rejected, (unsigned long) p.diff, pb,
                              (unsigned long) p.ping_ms, p.status);
    }
}

static void b_refresh_chips()
{
    const MinerState &s = g_state;
    float per = s.expected_ghs / AF_NUM_ASICS, best = 1;
    for (int i = 0; i < AF_NUM_ASICS; i++)
        if (s.chip_ghs[i] > best) best = s.chip_ghs[i];
    for (int i = 0; i < AF_NUM_ASICS; i++) {
        lv_label_set_text_fmt(b_chip_ghs[i], "%.0f", s.chip_ghs[i]);
        const float t = s.chip_temp[i];
        lv_label_set_text_fmt(b_chip_t[i], "%s%.0f\xC2\xB0" "C", s.chip_temp_own[i] ? "" : "~", t);
        uint32_t top = t >= CONFIG_AF_MAX_CHIP_TEMP ? D_RED : t >= CONFIG_AF_MAX_CHIP_TEMP - 6 ? D_HOT : D_LIT;
        set_crate(b_chip[i], s.chip_ghs[i] > 1, top);
        if (b_chip_err[i]) {
            float tot = s.chip_ghs[i] + s.chip_err_ghs[i];
            lv_label_set_text_fmt(b_chip_err[i], "err %.2f%%", tot > 0 ? 100.0f * s.chip_err_ghs[i] / tot : 0.0f);
        }
        lv_bar_set_value(b_chip_bar[i], (int) (100 * s.chip_ghs[i] / (per > best ? per : best)), LV_ANIM_ON);
    }
    const TunerState &t = s.tuner;
    lv_label_set_text(b_td_phase, phase_name(t.phase));
    if (t.best_freq) {
        char hv[16];
        const char *hu;
        fmt_hash(t.best_ghs, hv, sizeof(hv), &hu);
        lv_label_set_text_fmt(b_td_best, "Best  %u MHz @ %u mV  (%s %s)", t.best_freq, t.best_mv, hv, hu);
    } else {
        lv_label_set_text(b_td_best, "Best  searching...");
    }
    if (t.trial_freq) lv_label_set_text_fmt(b_td_trial, "trial %u MHz @ %u mV  %u steps", t.trial_freq, t.trial_mv, t.steps_done);
    else lv_label_set_text_fmt(b_td_trial, "%u steps tested", t.steps_done);
    lv_label_set_text(b_td_note, t.note);
}

static void b_refresh_system()
{
    const MinerState &s = g_state;
    char qr[192];
    static const char *k_run[7] = {"host", "ip", "wifi", "uptime", "chips", "firmware", "free heap"};
    static const char *k_ap[7] = {"SETUP", "1. join wifi", "password", "2. open", "3. enter", "", ""};
    const bool ap = s.ap_ssid[0] && !s.ip[0];
    for (int i = 0; i < 7; i++) lv_label_set_text(b_key[i], ap ? k_ap[i] : k_run[i]);
    if (ap) {
        if (s.ap_pass[0]) snprintf(qr, sizeof(qr), "WIFI:T:WPA;S:%s;P:%s;;", s.ap_ssid, s.ap_pass);
        else snprintf(qr, sizeof(qr), "WIFI:T:nopass;S:%s;;", s.ap_ssid);
        lv_label_set_text(b_val[0], "");
        lv_label_set_text(b_val[1], s.ap_ssid);
        lv_label_set_text(b_val[2], s.ap_pass[0] ? s.ap_pass : "none");
        lv_label_set_text(b_val[3], "192.168.4.1");
        lv_label_set_text(b_val[4], "wifi + pool");
        lv_label_set_text(b_val[5], "");
        lv_label_set_text(b_val[6], "");
    } else {
        snprintf(qr, sizeof(qr), "http://%s/", s.ip);
        uint32_t u = s.uptime_s;
        lv_label_set_text(b_val[0], s.hostname);
        lv_label_set_text(b_val[1], s.ip);
        lv_label_set_text(b_val[2], s.ssid);
        lv_label_set_text_fmt(b_val[3], "%lud %02luh %02lum", (unsigned long) (u / 86400), (unsigned long) (u % 86400 / 3600),
                              (unsigned long) (u % 3600 / 60));
        lv_label_set_text_fmt(b_val[4], "%d x %s", s.chips_detected, asic::MODEL);
        lv_label_set_text(b_val[5], esp_app_get_description()->version);
        lv_label_set_text_fmt(b_val[6], "%u kB", (unsigned) (esp_get_free_heap_size() / 1024));
    }
    if (strcmp(qr, s_qr_text)) {
        snprintf(s_qr_text, sizeof(s_qr_text), "%s", qr);
        lv_qrcode_update(b_qr, s_qr_text, strlen(s_qr_text));
    }
}

static bool b_refresh(int kind)
{
    switch (kind) {
    case K_DASH: b_refresh_dash(); return true;
    case K_CLOCK: b_refresh_clock(); return true;
    case K_BLOCKS: b_refresh_blocks(); return true;
    case K_POOL: b_refresh_pool(); return true;
    case K_CHIPS: b_refresh_chips(); return true;
    case K_SYSTEM: b_refresh_system(); return true;
    }
    return false;  // the custom pages refresh as in every theme
}
