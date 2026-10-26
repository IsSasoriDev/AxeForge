// Custom pages built from layout_store JSON. Included by ui.cpp.

#define CELL_W (LCD_H_RES / layout_store::COLS)                      // 40
#define CELL_H ((LCD_V_RES - HEADER_H - 14) / layout_store::ROWS)    // 34
#define MAX_WIDGETS 16

enum class WType : uint8_t {
    Text, Hashrate, Gauge, Chart, ChipTemp, VrTemp, Power, Eff, Fan, Chips, Clock, Date, Weather,
    Shares, BestDiff, Height, NetDiff, LastHash, Blocks, NextBlock, Fees, Tuner, Uptime, Reward, Unknown
};

static const struct {
    const char *id;
    WType t;
    const char *cap;
    lv_color_t (*accent)();
} WTYPES[] = {
    {"text", WType::Text, "", [] { return C_DIM; }},
    {"hashrate", WType::Hashrate, "HASHRATE", [] { return C_CYAN; }},
    {"gauge", WType::Gauge, "", [] { return C_CYAN; }},
    {"chart", WType::Chart, "HASHRATE 1h", [] { return C_CYAN; }},
    {"chiptemp", WType::ChipTemp, "ASIC TEMP", [] { return C_ORANGE; }},
    {"vrtemp", WType::VrTemp, "VR TEMP", [] { return C_VIOLET; }},
    {"power", WType::Power, "POWER", [] { return C_MAGENTA; }},
    {"eff", WType::Eff, "EFFICIENCY", [] { return C_GREEN; }},
    {"fan", WType::Fan, "FAN", [] { return C_CYAN; }},
    {"chips", WType::Chips, "ASICS", [] { return C_CYAN; }},
    {"clock", WType::Clock, "", [] { return C_CYAN; }},
    {"date", WType::Date, "", [] { return C_MAGENTA; }},
    {"weather", WType::Weather, "", [] { return C_SUN; }},
    {"shares", WType::Shares, "SHARES", [] { return C_GREEN; }},
    {"bestdiff", WType::BestDiff, "BEST DIFF", [] { return C_MAGENTA; }},
    {"height", WType::Height, "BLOCK", [] { return C_CYAN; }},
    {"netdiff", WType::NetDiff, "NETWORK DIFF", [] { return C_VIOLET; }},
    {"lasthash", WType::LastHash, "LIVE SHARE HASH", [] { return C_CYAN; }},
    {"blocks", WType::Blocks, "MEMPOOL", [] { return C_VIOLET; }},
    {"nextblock", WType::NextBlock, "NEXT BLOCK", [] { return C_ORANGE; }},
    {"fees", WType::Fees, "FEES sat/vB", [] { return C_ORANGE; }},
    {"tuner", WType::Tuner, "AUTO-TUNE", [] { return C_VIOLET; }},
    {"uptime", WType::Uptime, "UPTIME", [] { return C_DIM; }},
    {"reward", WType::Reward, "BLOCK REWARD", [] { return C_GREEN; }},
};

struct Widget {
    WType type = WType::Unknown;
    int wpx = 0, hpx = 0, rows = 1;
    lv_obj_t *box = nullptr, *cap = nullptr, *val = nullptr, *sub = nullptr, *aux = nullptr;
    lv_obj_t *parts[6] = {};
    lv_chart_series_t *series = nullptr;
    int drawn = -2;
};

struct CustomPage {
    Widget w[MAX_WIDGETS];
    int n = 0;
};

static CustomPage s_custom[layout_store::PAGES];
static lv_obj_t *s_custom_root[layout_store::PAGES];

static const lv_font_t *value_font(int hpx, int wpx)
{
    if (hpx >= 120 && wpx >= 160) return &lv_font_montserrat_48;
    if (hpx >= 90) return &lv_font_montserrat_28;
    if (hpx >= 56) return &lv_font_montserrat_20;
    return &lv_font_montserrat_14;
}

static void build_widget(lv_obj_t *page, Widget &w, const char *type, int gx, int gy, int gw, int gh, const char *txt)
{
    int idx = -1;
    for (int i = 0; i < (int) (sizeof(WTYPES) / sizeof(WTYPES[0])); i++)
        if (!strcmp(WTYPES[i].id, type)) idx = i;
    w = Widget();
    w.type = idx >= 0 ? WTYPES[idx].t : WType::Unknown;
    w.wpx = gw * CELL_W - 4;
    w.hpx = gh * CELL_H - 4;
    w.rows = gh;
    w.box = panel(page, gx * CELL_W + 2, gy * CELL_H + 2, w.wpx, w.hpx);
    lv_color_t accent = idx >= 0 ? WTYPES[idx].accent() : C_DIM;
    const char *cap = idx >= 0 ? WTYPES[idx].cap : type;
    bool tiny = w.hpx < 40;

    if (cap[0] && !tiny) {
        blob(w.box, 0, 7, 3, 12, accent, 0);
        w.cap = label(w.box, &lv_font_montserrat_12, C_DIM, cap);
        lv_obj_set_pos(w.cap, 9, 4);
    }
    const lv_font_t *vf = value_font(w.hpx - (w.cap ? 18 : 0), w.wpx);

    switch (w.type) {
    case WType::Gauge: {
        int d = (w.hpx < w.wpx ? w.hpx : w.wpx) - 8;
        w.aux = lv_arc_create(w.box);
        lv_obj_set_size(w.aux, d, d);
        lv_obj_center(w.aux);
        lv_arc_set_bg_angles(w.aux, 135, 45);
        lv_arc_set_range(w.aux, 0, 1000);
        lv_obj_remove_style(w.aux, NULL, LV_PART_KNOB);
        lv_obj_clear_flag(w.aux, LV_OBJ_FLAG_CLICKABLE);
        int aw = d / 12 + 2;
        lv_obj_set_style_arc_width(w.aux, aw, LV_PART_MAIN);
        lv_obj_set_style_arc_color(w.aux, C_TRACK, LV_PART_MAIN);
        lv_obj_set_style_arc_width(w.aux, aw, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(w.aux, C_CYAN, LV_PART_INDICATOR);
        w.val = label(w.box, d > 140 ? &lv_font_montserrat_48 : d > 90 ? &lv_font_montserrat_28 : &lv_font_montserrat_20, C_TEXT, "--");
        lv_obj_align(w.val, LV_ALIGN_CENTER, 0, -4);
        w.sub = label(w.box, &lv_font_montserrat_14, C_CYAN, "");
        lv_obj_align(w.sub, LV_ALIGN_CENTER, 0, d / 5 + 4);
        return;
    }
    case WType::Chart: {
        w.aux = lv_chart_create(w.box);
        lv_obj_set_size(w.aux, w.wpx - 16, w.hpx - (w.cap ? 26 : 12));
        lv_obj_align(w.aux, LV_ALIGN_BOTTOM_MID, 0, -6);
        lv_obj_set_style_bg_opa(w.aux, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(w.aux, 0, 0);
        lv_obj_set_style_pad_all(w.aux, 0, 0);
        lv_obj_set_style_line_color(w.aux, C_TRACK, LV_PART_MAIN);
        lv_chart_set_div_line_count(w.aux, 3, 0);
        lv_chart_set_point_count(w.aux, AF_HISTORY_LEN);
        lv_obj_set_style_size(w.aux, 0, LV_PART_INDICATOR);
        lv_obj_set_style_line_width(w.aux, 2, LV_PART_ITEMS);
        w.series = lv_chart_add_series(w.aux, C_CYAN, LV_CHART_AXIS_PRIMARY_Y);
        return;
    }
    case WType::Chips: {
        int rowh = (w.hpx - 24) / 4;
        for (int i = 0; i < 4; i++) {
            lv_obj_t *b = thin_bar(w.box, C_CYAN);
            lv_obj_set_size(b, w.wpx - 90, rowh > 14 ? 6 : 4);
            lv_obj_set_pos(b, 30, 24 + i * rowh + rowh / 2 - 3);
            w.parts[i] = b;
            lv_obj_t *l = label(w.box, &lv_font_montserrat_12, C_DIM, "");
            lv_obj_set_pos(l, 9, 24 + i * rowh + rowh / 2 - 7);
            lv_label_set_text_fmt(l, "%d", i + 1);
            lv_obj_t *v = label(w.box, &lv_font_montserrat_12, C_TEXT, "");
            lv_obj_set_pos(v, w.wpx - 56, 24 + i * rowh + rowh / 2 - 7);
            if (i < 4) w.parts[i] = b;
            if (i == 0) w.aux = v;
            lv_obj_set_user_data(b, v);
        }
        return;
    }
    case WType::Clock: {
        const lv_font_t *f = (w.hpx >= 100 && w.wpx >= 330) ? &font_clock_110
                             : w.hpx >= 60                   ? &lv_font_montserrat_48
                                                             : &lv_font_montserrat_28;
        w.val = label(w.box, f, C_TEXT, "--:--");
        lv_obj_align(w.val, LV_ALIGN_CENTER, 0, w.hpx >= 100 ? -8 : 0);
        w.sub = label(w.box, &lv_font_montserrat_14, C_MAGENTA, "");
        lv_obj_align(w.sub, LV_ALIGN_BOTTOM_MID, 0, -4);
        if (w.hpx < 100) lv_obj_add_flag(w.sub, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    case WType::Weather: {
        int isz = w.hpx - 20 > 80 ? 80 : w.hpx - 20;
        if (isz < 24) isz = 24;
        w.aux = icon_box(w.box, 8, (w.hpx - isz) / 2, isz);
        w.val = label(w.box, w.hpx >= 100 ? &font_temp_64 : &lv_font_montserrat_28, C_TEXT, "--");
        lv_obj_set_pos(w.val, isz + 16, w.hpx >= 100 ? 10 : 4);
        w.sub = label(w.box, &lv_font_montserrat_14, C_TEXT, "");
        lv_obj_set_width(w.sub, w.wpx - isz - 24);
        lv_label_set_long_mode(w.sub, LV_LABEL_LONG_DOT);
        lv_obj_align_to(w.sub, w.val, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);
        return;
    }
    case WType::LastHash: {
        w.val = lv_label_create(w.box);
        lv_label_set_recolor(w.val, true);
        lv_obj_set_style_text_font(w.val, w.wpx >= 300 ? &lv_font_montserrat_14 : &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(w.val, C_DIM, 0);
        lv_obj_set_pos(w.val, 9, w.cap ? 22 : 6);
        lv_label_set_text(w.val, "");
        w.sub = label(w.box, &lv_font_montserrat_12, C_DIM, "");
        lv_obj_align(w.sub, LV_ALIGN_BOTTOM_LEFT, 9, -4);
        return;
    }
    case WType::Blocks: {
        int n = w.wpx / 70;
        if (n > 5) n = 5;
        if (n < 1) n = 1;
        int sz = (w.wpx - 12) / n - 8;
        int hs = w.hpx - (w.cap ? 30 : 12);
        if (sz > hs) sz = hs;
        for (int i = 0; i < n && i < 6; i++) {
            lv_obj_t *c = blob(w.box, 8 + i * (sz + 8), w.cap ? 22 : 6, sz, sz, C_PANEL, 5);
            lv_obj_set_style_bg_grad_dir(c, LV_GRAD_DIR_VER, 0);
            lv_obj_t *l = label(c, sz > 50 ? &lv_font_montserrat_12 : &lv_font_montserrat_12, lv_color_hex(0xffffff), "");
            lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_center(l);
            lv_obj_set_user_data(c, l);
            w.parts[i] = c;
        }
        return;
    }
    default:
        break;
    }

    // generic stat: caption, big value, small sub line
    w.val = lv_label_create(w.box);
    lv_label_set_recolor(w.val, true);
    lv_obj_set_style_text_font(w.val, vf, 0);
    lv_obj_set_style_text_color(w.val, C_TEXT, 0);
    lv_label_set_text(w.val, w.type == WType::Text ? (txt && txt[0] ? txt : "Text") : "--");
    lv_label_set_long_mode(w.val, LV_LABEL_LONG_DOT);
    lv_obj_set_width(w.val, w.wpx - 16);
    if (tiny) lv_obj_align(w.val, LV_ALIGN_LEFT_MID, 8, 0);
    else lv_obj_align(w.val, LV_ALIGN_LEFT_MID, 9, w.cap ? 6 : 0);
    if (w.type == WType::Text) {
        lv_obj_set_style_text_align(w.val, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(w.val, LV_ALIGN_CENTER, 0, 0);
    }
    if (!tiny) {
        w.sub = label(w.box, &lv_font_montserrat_12, C_DIM, "");
        // narrow tiles: put the detail line at the bottom so it can't collide with the caption
        if (w.wpx < 200 && w.hpx >= 56) lv_obj_align(w.sub, LV_ALIGN_BOTTOM_RIGHT, -8, -4);
        else if (w.wpx < 200) lv_obj_add_flag(w.sub, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_align(w.sub, LV_ALIGN_TOP_RIGHT, -8, 4);
    }
}

static void build_custom(int pi)
{
    lv_obj_t *page = s_custom_root[pi];
    lv_obj_clean(page);
    CustomPage &cp = s_custom[pi];
    cp.n = 0;
    cJSON *root = cJSON_Parse(layout_store::json());
    cJSON *list = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "pages"), pi);
    cJSON *it;
    cJSON_ArrayForEach(it, list)
    {
        if (cp.n >= MAX_WIDGETS) break;
        cJSON *t = cJSON_GetObjectItem(it, "t"), *txt = cJSON_GetObjectItem(it, "txt");
        build_widget(page, cp.w[cp.n++], cJSON_IsString(t) ? t->valuestring : "?",
                     cJSON_GetObjectItem(it, "x")->valueint, cJSON_GetObjectItem(it, "y")->valueint,
                     cJSON_GetObjectItem(it, "w")->valueint, cJSON_GetObjectItem(it, "h")->valueint,
                     cJSON_IsString(txt) ? txt->valuestring : "");
    }
    cJSON_Delete(root);
    if (!cp.n) {
        lv_obj_t *l = label(page, &lv_font_montserrat_14, C_DIM, "Empty custom page - design it in\nthe web UI > Display > Layout editor");
        lv_obj_center(l);
    }
}

static void set_stat(Widget &w, const char *val, const char *sub)
{
    if (w.val) lv_label_set_text(w.val, val);
    if (w.sub) lv_label_set_text(w.sub, sub);
}

static void refresh_widget(Widget &w)
{
    const MinerState &s = g_state;
    const Settings &cfg = settings::get();
    char v[96], sub[48] = "", hv[16], d[24];
    const char *hu;
    switch (w.type) {
    case WType::Hashrate:
        fmt_hash(s.hashrate_ghs, hv, sizeof(hv), &hu);
        snprintf(v, sizeof(v), "%s #00e5ff %s#", hv, hu);
        snprintf(sub, sizeof(sub), "err %.2f%%", s.error_pct);
        set_stat(w, v, sub);
        break;
    case WType::Gauge: {
        fmt_hash(s.hashrate_ghs, hv, sizeof(hv), &hu);
        float max_ghs = board::limits().max_freq_mhz * 2040.0f * AF_NUM_ASICS / 1000.0f;
        lv_arc_set_value(w.aux, (int16_t) (1000.0f * s.hashrate_ghs / max_ghs));
        set_stat(w, hv, hu);
        break;
    }
    case WType::Chart: {
        float vmax = 1;
        for (int i = 0; i < AF_HISTORY_LEN; i++)
            if (s.history[i] > vmax) vmax = s.history[i];
        lv_chart_set_range(w.aux, LV_CHART_AXIS_PRIMARY_Y, 0, (lv_coord_t) (vmax * 1.1f));
        for (int i = 0; i < AF_HISTORY_LEN; i++) {
            float h = s.history[(s.history_head + i) % AF_HISTORY_LEN];
            w.series->y_points[i] = h > 0 ? (lv_coord_t) h : LV_CHART_POINT_NONE;
        }
        lv_chart_refresh(w.aux);
        break;
    }
    case WType::ChipTemp:
        snprintf(v, sizeof(v), "%.0f\xC2\xB0" "C", s.chip_temp_max);
        snprintf(sub, sizeof(sub), "max %d", CONFIG_AF_MAX_CHIP_TEMP);
        set_stat(w, v, sub);
        lv_obj_set_style_text_color(w.val, temp_color(s.chip_temp_max, CONFIG_AF_MAX_CHIP_TEMP - 6, CONFIG_AF_MAX_CHIP_TEMP), 0);
        break;
    case WType::VrTemp:
        if (AF_HAS_VR_SENSOR) snprintf(v, sizeof(v), "%.0f\xC2\xB0" "C", s.vr_temp);
        else snprintf(v, sizeof(v), "--");
        snprintf(sub, sizeof(sub), "%.2fV", s.vout);
        set_stat(w, v, sub);
        break;
    case WType::Power:
        snprintf(v, sizeof(v), "%.0f W", s.pin);
        snprintf(sub, sizeof(sub), "%.1fV in", s.vin);
        set_stat(w, v, sub);
        break;
    case WType::Eff:
        snprintf(v, sizeof(v), "%.1f #9aa8bd J/TH#", s.hashrate_ghs > 1 ? s.pin / (s.hashrate_ghs / 1000.0f) : 0.0f);
        set_stat(w, v, "");
        break;
    case WType::Fan:
        snprintf(v, sizeof(v), "%u%%", s.fan_pct);
        if (s.fan_present[0] && s.fan_present[1]) snprintf(sub, sizeof(sub), "2 fans %u/%u", s.fan_rpm[0], s.fan_rpm[1]);
        else snprintf(sub, sizeof(sub), "%u rpm", s.fan_present[1] ? s.fan_rpm[1] : s.fan_rpm[0]);
        set_stat(w, v, sub);
        break;
    case WType::Chips: {
        float per = s.expected_ghs / AF_NUM_ASICS;
        for (int i = 0; i < 4; i++) {
            lv_obj_t *b = w.parts[i];
            lv_bar_set_value(b, per > 0 ? (int) (100 * s.chip_ghs[i] / per) : 0, LV_ANIM_ON);
            lv_label_set_text_fmt((lv_obj_t *) lv_obj_get_user_data(b), "%.0f G", s.chip_ghs[i]);
        }
        break;
    }
    case WType::Clock:
    case WType::Date: {
        static const char *wd[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        static const char *mo[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        struct tm t;
        if (!weather::local_time(&t)) {
            set_stat(w, "--:--", "syncing");
            break;
        }
        int h = t.tm_hour;
        if (!cfg.clock_24h) h = h % 12 ? h % 12 : 12;
        snprintf(sub, sizeof(sub), "%s %d %s", wd[t.tm_wday], t.tm_mday, mo[t.tm_mon]);
        if (w.type == WType::Clock) {
            snprintf(v, sizeof(v), "%02d:%02d", h, t.tm_min);  // the big font has digits and ':' only
            set_stat(w, v, sub);
            lv_obj_align(w.val, LV_ALIGN_CENTER, 0, w.hpx >= 100 ? -8 : 0);
        } else {
            set_stat(w, sub, "");
        }
        break;
    }
    case WType::Weather: {
        const WeatherState &wx = s.weather;
        if (!wx.valid) {
            set_stat(w, "--", wx.error[0] ? wx.error : "no weather");
            break;
        }
        snprintf(v, sizeof(v), "%.0f\xC2\xB0", wx.temp);
        if (w.wpx >= 260) snprintf(sub, sizeof(sub), "%s, %s", weather::describe(wx.code), wx.city);
        else snprintf(sub, sizeof(sub), "%s", weather::describe(wx.code));
        set_stat(w, v, sub);
        int key = wx.code * 2 + wx.is_day;
        if (w.drawn != key) {
            w.drawn = key;
            draw_weather_icon(w.aux, wx.code, wx.is_day, C_PANEL);
        }
        break;
    }
    case WType::Shares: {
        uint32_t tot = s.shares_accepted + s.shares_rejected;
        snprintf(v, sizeof(v), "#2bff88 %lu# / #ff3b5c %lu#", (unsigned long) s.shares_accepted, (unsigned long) s.shares_rejected);
        snprintf(sub, sizeof(sub), "%.2f%% rej", tot ? 100.0f * s.shares_rejected / tot : 0.0f);
        set_stat(w, v, sub);
        break;
    }
    case WType::BestDiff:
        fmt_diff(s.best_diff, v, sizeof(v));
        fmt_diff(s.session_best, d, sizeof(d));
        snprintf(sub, sizeof(sub), "now %s", d);
        set_stat(w, v, sub);
        break;
    case WType::Height:
        snprintf(v, sizeof(v), "%lu", (unsigned long) s.block_height);
        set_stat(w, v, s.tmpl.valid ? s.pools[s.tmpl.pool < 0 ? 0 : s.tmpl.pool].host : "");
        break;
    case WType::NetDiff:
        fmt_diff(work::nbits_to_difficulty(s.network_nbits), v, sizeof(v));
        set_stat(w, v, "");
        break;
    case WType::Reward:
        snprintf(v, sizeof(v), "%.4f #9aa8bd BTC#", s.tmpl.reward_btc);
        set_stat(w, v, s.tmpl.tag);
        break;
    case WType::LastHash: {
        if (!s.last_hash[0]) break;
        int per = w.wpx >= 300 ? 32 : 16;
        int lines = (w.hpx - (w.cap ? 40 : 22)) / 17;
        if (lines < 1) lines = 1;
        int show = per * lines > 64 ? 64 : per * lines;
        char buf[200];
        int o = 0, z = 0;
        while (z < 64 && s.last_hash[z] == '0') z++;
        bool colored = false;
        for (int i = 0; i < show; i++) {
            if (i && i % per == 0) {
                if (colored) buf[o++] = '#';
                buf[o++] = '\n';
                colored = false;
            }
            if (i < z && !colored) {
                o += snprintf(buf + o, sizeof(buf) - o, "#00e5ff ");
                colored = true;
            }
            if (i == z && colored) {
                buf[o++] = '#';
                colored = false;
            }
            buf[o++] = s.last_hash[i];
        }
        if (colored) buf[o++] = '#';
        buf[o] = 0;
        lv_label_set_text(w.val, buf);
        fmt_diff(s.last_hash_diff, d, sizeof(d));
        lv_label_set_text_fmt(w.sub, "%d zeros  diff %s", z, d);
        break;
    }
    case WType::Blocks: {
        const MempoolState &m = s.mempool;
        for (int i = 0; i < 6 && w.parts[i]; i++) {
            lv_obj_t *c = w.parts[i];
            lv_obj_t *l = (lv_obj_t *) lv_obj_get_user_data(c);
            if (i == 0) {
                lv_color_t col = m.valid ? fee_color(m.next[0].median_fee) : C_PANEL;
                lv_obj_set_style_bg_color(c, col, 0);
                lv_obj_set_style_bg_grad_color(c, lv_color_darken(col, LV_OPA_40), 0);
                lv_label_set_text_fmt(l, "next\n~%.0f s/vB", m.next[0].median_fee);
            } else {
                const MempoolBlock &b = m.mined[i - 1];
                lv_color_t col = m.valid ? lv_color_mix(fee_color(b.median_fee), lv_color_hex(0x4a3fb0), LV_OPA_50) : C_PANEL;
                lv_obj_set_style_bg_color(c, col, 0);
                lv_obj_set_style_bg_grad_color(c, lv_color_darken(col, LV_OPA_40), 0);
                lv_label_set_text_fmt(l, "%lu\n%.10s", (unsigned long) b.height, b.pool);
            }
        }
        break;
    }
    case WType::NextBlock: {
        const MempoolNext &n = s.mempool.next[0];
        snprintf(v, sizeof(v), "~%.0f #9aa8bd sat/vB#", n.median_fee);
        snprintf(sub, sizeof(sub), "%lu txs", (unsigned long) n.txs);
        set_stat(w, v, sub);
        break;
    }
    case WType::Fees: {
        const MempoolState &m = s.mempool;
        snprintf(v, sizeof(v), "#ff9f1c %.0f# #9aa8bd /# %.0f #9aa8bd /# #2bff88 %.0f#", m.fee_fast, m.fee_hour, m.fee_eco);
        set_stat(w, v, "fast/hour/eco");
        break;
    }
    case WType::Tuner:
        snprintf(v, sizeof(v), "%u MHz  %u.%02u V", s.freq_mhz, s.core_mv / 1000, (s.core_mv % 1000) / 10);
        set_stat(w, v, phase_name(s.tuner.phase));
        break;
    case WType::Uptime: {
        uint32_t u = s.uptime_s;
        snprintf(v, sizeof(v), "%lud %02luh %02lum", (unsigned long) (u / 86400), (unsigned long) (u % 86400 / 3600),
                 (unsigned long) (u % 3600 / 60));
        set_stat(w, v, "");
        break;
    }
    default:
        break;
    }
}

static void refresh_custom(int pi)
{
    CustomPage &cp = s_custom[pi];
    for (int i = 0; i < cp.n; i++) refresh_widget(cp.w[i]);
}
