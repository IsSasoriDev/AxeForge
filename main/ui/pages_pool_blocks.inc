// Pool and Blocks pages. Included by ui.cpp so they share its helpers.

// ------------------------------------------------------------------ pool page
struct StatTile {
    lv_obj_t *val, *sub;
};

struct PoolCard {
    lv_obj_t *box, *dot, *host, *badge, *badge_lbl, *main, *line1, *line2, *bar;
};

static StatTile s_pt_acc, s_pt_rej, s_pt_best, s_pt_blk;
static PoolCard s_pcards[AF_POOLS];

static StatTile stat_tile(lv_obj_t *p, int x, int y, int w, int h, const char *cap, lv_color_t accent)
{
    StatTile t;
    lv_obj_t *b = panel(p, x, y, w, h);
    lv_obj_t *stripe = blob(b, 0, 7, 3, 14, accent, 0);
    (void) stripe;
    lv_obj_t *c = label(b, &lv_font_montserrat_12, C_DIM, cap);
    lv_obj_set_pos(c, 10, 5);
    t.val = label(b, &lv_font_montserrat_20, C_TEXT, "--");
    lv_obj_set_pos(t.val, 10, 23);
    t.sub = label(b, &lv_font_montserrat_12, C_DIM, "");
    lv_obj_align(t.sub, LV_ALIGN_BOTTOM_RIGHT, -8, -6);  // under the caption line, never on it
    return t;
}

static void build_pool(lv_obj_t *p)
{
    const int tw = 114, gap = 4;
    s_pt_acc = stat_tile(p, 6, 4, tw, 52, "ACCEPTED", C_GREEN);
    s_pt_rej = stat_tile(p, 6 + (tw + gap), 4, tw, 52, "REJECTED", C_RED);
    s_pt_best = stat_tile(p, 6 + 2 * (tw + gap), 4, tw, 52, "BEST DIFF", C_MAGENTA);
    s_pt_blk = stat_tile(p, 6 + 3 * (tw + gap), 4, tw, 52, "BLOCKS", C_CYAN);

    const int cw = 232, ch = 104;
    for (int i = 0; i < AF_POOLS; i++) {
        PoolCard &c = s_pcards[i];
        c.box = panel(p, 6 + (i % 2) * (cw + 4), 62 + (i / 2) * (ch + 4), cw, ch);
        c.dot = blob(c.box, 10, 11, 8, 8, C_DIM, 4);
        c.host = label(c.box, &lv_font_montserrat_14, C_TEXT, "");
        lv_obj_set_width(c.host, cw - 80);
        lv_label_set_long_mode(c.host, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(c.host, 24, 6);
        c.badge = blob(c.box, cw - 50, 6, 40, 16, C_BORDER, 8);
        if (theme::paper()) {  // outlined tag instead of a filled pill
            lv_obj_set_style_border_width(c.badge, 1, 0);
            lv_obj_set_style_border_color(c.badge, C_TEXT, 0);
        }
        c.badge_lbl = label(c.badge, &lv_font_montserrat_12, C_TEXT, "");
        lv_obj_center(c.badge_lbl);
        c.main = lv_label_create(c.box);
        lv_label_set_recolor(c.main, true);
        lv_obj_set_style_text_font(c.main, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(c.main, C_TEXT, 0);
        lv_obj_set_pos(c.main, 10, 28);
        lv_label_set_text(c.main, "");
        c.line1 = lv_label_create(c.box);
        lv_label_set_recolor(c.line1, true);
        lv_obj_set_style_text_font(c.line1, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(c.line1, C_DIM, 0);
        lv_obj_set_pos(c.line1, 10, 56);
        lv_label_set_text(c.line1, "");
        c.line2 = label(c.box, &lv_font_montserrat_12, C_DIM, "");
        lv_obj_set_width(c.line2, cw - 20);
        lv_label_set_long_mode(c.line2, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(c.line2, 10, 72);
        c.bar = thin_bar(c.box, C_CYAN);
        lv_obj_set_size(c.bar, cw - 20, 3);
        lv_obj_align(c.bar, LV_ALIGN_BOTTOM_MID, 0, -7);
    }
}

static void refresh_pool()
{
    const MinerState &s = g_state;
    char a[24], b[24];
    uint32_t total = s.shares_accepted + s.shares_rejected;
    lv_label_set_text_fmt(s_pt_acc.val, "%lu", (unsigned long) s.shares_accepted);
    lv_label_set_text_fmt(s_pt_rej.val, "%lu", (unsigned long) s.shares_rejected);
    lv_label_set_text_fmt(s_pt_rej.sub, "%.2f%%", total ? 100.0f * s.shares_rejected / total : 0.0f);
    fmt_diff(s.best_diff, a, sizeof(a));
    fmt_diff(s.session_best, b, sizeof(b));
    lv_label_set_text(s_pt_best.val, a);
    lv_label_set_text_fmt(s_pt_best.sub, "now %s", b);
    lv_label_set_text_fmt(s_pt_blk.val, "%lu", (unsigned long) s.blocks_found);
    lv_label_set_text(s_pt_blk.sub, settings::get().pool_mode == PoolMode::Split ? "split" : "failover");

    static const char *proto[] = {"V1", "TLS", "SV2"};
    static const lv_color_t pcol[] = {C_BORDER, C_VIOLET, C_MAGENTA};
    for (int i = 0; i < AF_POOLS; i++) {
        const PoolState &ps = s.pools[i];
        PoolCard &c = s_pcards[i];
        lv_obj_set_style_opa(c.box, ps.enabled ? LV_OPA_COVER : LV_OPA_40, 0);
        lv_obj_set_style_bg_color(c.dot, !ps.enabled ? C_DIM : ps.connected ? C_GREEN : C_ORANGE, 0);
        if (!ps.enabled) {
            lv_label_set_text_fmt(c.host, "Pool %d", i + 1);
            lv_label_set_text(c.main, "#9aa8bd not set up#");
            lv_label_set_text(c.line1, "");
            lv_label_set_text(c.line2, "Add it in the web UI > Pools");
            lv_obj_add_flag(c.badge, LV_OBJ_FLAG_HIDDEN);
            lv_bar_set_value(c.bar, 0, LV_ANIM_OFF);
            continue;
        }
        lv_obj_clear_flag(c.badge, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(c.host, ps.host);
        lv_label_set_text(c.badge_lbl, proto[ps.proto < 3 ? ps.proto : 0]);
        lv_obj_set_style_bg_color(c.badge, theme::paper() ? C_PANEL : pcol[ps.proto < 3 ? ps.proto : 0], 0);
        char hv[16];
        const char *hu;
        fmt_hash(ps.ghs, hv, sizeof(hv), &hu);
        lv_label_set_text_fmt(c.main, "%u%%  #00e5ff %s %s#", ps.work_pct, hv, hu);
        fmt_diff(ps.best, a, sizeof(a));
        lv_label_set_text_fmt(c.line1, "#2bff88 %lu# / #ff3b5c %lu#   diff %lu   best %s   %lums", (unsigned long) ps.accepted,
                              (unsigned long) ps.rejected, (unsigned long) ps.diff, a, (unsigned long) ps.ping_ms);
        lv_label_set_text(c.line2, ps.status);
        lv_bar_set_value(c.bar, ps.work_pct, LV_ANIM_ON);
    }
}

// ------------------------------------------------------------------ blocks page
struct Cube {
    lv_obj_t *back, *face, *l1, *l2, *l3, *top;
};

static Cube s_next[2], s_mined[3];
static lv_obj_t *s_now_h, *s_now_pool, *s_now_reward, *s_now_tag, *s_now_prev, *s_live_hash, *s_live_diff, *s_mp_err;

static lv_color_t fee_color(float fee)
{
    if (fee < 2) return lv_color_hex(0x5b3fd9);
    if (fee < 5) return lv_color_hex(0x3d7bff);
    if (fee < 10) return lv_color_hex(0x1fbfa0);
    if (fee < 20) return lv_color_hex(0x8fd12b);
    if (fee < 50) return lv_color_hex(0xffb21c);
    return lv_color_hex(0xff3b5c);
}

static Cube make_cube(lv_obj_t *p, int x, int y, int sz)
{
    Cube c;
    c.top = label(p, &lv_font_montserrat_12, C_DIM, "");
    lv_obj_set_width(c.top, sz + 6);
    lv_obj_set_style_text_align(c.top, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(c.top, x, y);
    c.back = blob(p, x + 6, y + 16, sz, sz, C_BORDER, 6);  // depth layer for a 3D look
    c.face = blob(p, x, y + 22, sz, sz, C_PANEL, 6);
    lv_obj_set_style_bg_grad_dir(c.face, LV_GRAD_DIR_VER, 0);
    // Paper draws the cubes as outlined boxes with ink text instead of fee-coloured blocks
    const lv_color_t txt = theme::paper() ? C_TEXT : lv_color_hex(0xffffff);
    if (theme::paper()) {
        lv_obj_set_style_border_width(c.face, 1, 0);
        lv_obj_set_style_border_color(c.face, C_TEXT, 0);
    }
    c.l1 = label(c.face, &lv_font_montserrat_14, txt, "");
    lv_obj_align(c.l1, LV_ALIGN_TOP_MID, 0, 8);
    c.l2 = label(c.face, &lv_font_montserrat_12, txt, "");
    lv_obj_align(c.l2, LV_ALIGN_TOP_MID, 0, 28);
    c.l3 = label(c.face, &lv_font_montserrat_12, txt, "");
    lv_obj_align(c.l3, LV_ALIGN_BOTTOM_MID, 0, -8);
    return c;
}

static void paint_cube(Cube &c, lv_color_t col)
{
    if (theme::paper()) {
        lv_obj_set_style_bg_color(c.face, C_PANEL, 0);
        lv_obj_set_style_bg_grad_color(c.face, C_PANEL, 0);
        lv_obj_set_style_bg_color(c.back, C_TRACK, 0);
        return;
    }
    lv_obj_set_style_bg_color(c.face, col, 0);
    lv_obj_set_style_bg_grad_color(c.face, lv_color_darken(col, LV_OPA_40), 0);
    lv_obj_set_style_bg_color(c.back, lv_color_darken(col, LV_OPA_70), 0);
}

static void build_blocks(lv_obj_t *p)
{
    const int sz = 78;
    for (int i = 0; i < 2; i++) s_next[i] = make_cube(p, 8 + (1 - i) * 92, 2, sz);  // soonest next to the line
    lv_obj_t *sep = blob(p, 194, 10, 2, 104, C_CYAN, 1);
    lv_obj_set_style_bg_opa(sep, LV_OPA_50, 0);
    lv_obj_t *pick = label(p, &lv_font_montserrat_12, C_CYAN, LV_SYMBOL_CHARGE);
    lv_obj_set_pos(pick, 189, 112);
    for (int i = 0; i < 3; i++) s_mined[i] = make_cube(p, 206 + i * 92, 2, sz);
    s_mp_err = label(p, &lv_font_montserrat_12, C_DIM, "");
    lv_obj_set_pos(s_mp_err, 206, 50);

    // what our chips are hashing
    lv_obj_t *n = panel(p, 6, 128, 268, 142);
    lv_obj_t *cap = label(n, &lv_font_montserrat_12, C_CYAN, LV_SYMBOL_CHARGE "  NOW MINING");
    lv_obj_set_pos(cap, 10, 6);
    s_now_pool = label(n, &lv_font_montserrat_12, C_DIM, "");
    lv_obj_align(s_now_pool, LV_ALIGN_TOP_RIGHT, -10, 6);
    s_now_h = label(n, &lv_font_montserrat_28, C_TEXT, "--");
    lv_obj_set_pos(s_now_h, 10, 24);
    s_now_reward = lv_label_create(n);
    lv_label_set_recolor(s_now_reward, true);
    lv_obj_set_style_text_font(s_now_reward, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_now_reward, C_TEXT, 0);
    lv_obj_set_pos(s_now_reward, 10, 60);
    lv_label_set_text(s_now_reward, "");
    s_now_tag = label(n, &lv_font_montserrat_12, C_MAGENTA, "");
    lv_obj_set_width(s_now_tag, 248);
    lv_label_set_long_mode(s_now_tag, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_pos(s_now_tag, 10, 84);
    s_now_prev = label(n, &lv_font_montserrat_12, C_DIM, "");
    lv_obj_set_width(s_now_prev, 248);
    lv_label_set_long_mode(s_now_prev, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(s_now_prev, 10, 104);

    lv_obj_t *h = panel(p, 280, 128, 194, 142);
    lv_obj_t *hc = label(h, &lv_font_montserrat_12, C_CYAN, LV_SYMBOL_SHUFFLE "  LIVE SHARE HASH");
    lv_obj_set_pos(hc, 10, 6);
    s_live_hash = lv_label_create(h);
    lv_label_set_recolor(s_live_hash, true);
    lv_obj_set_style_text_font(s_live_hash, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_live_hash, C_DIM, 0);
    lv_obj_set_style_text_letter_space(s_live_hash, 1, 0);
    lv_obj_set_style_text_line_space(s_live_hash, 2, 0);
    lv_obj_set_pos(s_live_hash, 10, 26);
    lv_label_set_text(s_live_hash, "");
    s_live_diff = label(h, &lv_font_montserrat_12, C_DIM, "");
    lv_obj_set_pos(s_live_diff, 10, 118);
}

static void refresh_blocks()
{
    const MinerState &s = g_state;
    const MempoolState &m = s.mempool;
    lv_label_set_text(s_mp_err, m.valid ? "" : (m.error[0] ? m.error : "Loading mempool..."));
    for (int i = 0; i < 2; i++) {
        Cube &c = s_next[i];
        const MempoolNext &nb = m.next[i];
        paint_cube(c, m.valid ? fee_color(nb.median_fee) : C_PANEL);
        lv_label_set_text_fmt(c.l1, "~%.0f sat/vB", nb.median_fee);
        lv_label_set_text_fmt(c.l2, "%.0f-%.0f", nb.min_fee, nb.max_fee);
        lv_label_set_text_fmt(c.l3, "%lu txs", (unsigned long) nb.txs);
        lv_label_set_text(c.top, i == 0 ? "next ~10m" : "~20m");
    }
    struct tm now;
    bool have_time = weather::local_time(&now);
    time_t now_t = time(NULL);
    for (int i = 0; i < 3; i++) {
        Cube &c = s_mined[i];
        const MempoolBlock &b = m.mined[i];
        paint_cube(c, m.valid ? lv_color_mix(fee_color(b.median_fee), lv_color_hex(0x4a3fb0), LV_OPA_50) : C_PANEL);
        lv_label_set_text_fmt(c.l1, "%lu", (unsigned long) b.height);
        lv_label_set_text_fmt(c.l2, "~%.0f sat/vB", b.median_fee);
        lv_label_set_text(c.l3, b.pool);
        if (have_time && b.time) lv_label_set_text_fmt(c.top, "%ld min ago", (long) ((now_t - b.time) / 60));
        else lv_label_set_text_fmt(c.top, "%lu txs", (unsigned long) b.txs);
    }

    const BlockTemplate &t = s.tmpl;
    if (t.valid) {
        lv_label_set_text_fmt(s_now_h, "%lu", (unsigned long) t.height);
        lv_label_set_text_fmt(s_now_pool, "pool %d %s", t.pool + 1, s.pools[t.pool < 0 ? 0 : t.pool].host);
        char nd[24];
        fmt_diff(work::nbits_to_difficulty(t.nbits), nd, sizeof(nd));
        lv_label_set_text_fmt(s_now_reward, "#2bff88 %.4f BTC#  #9aa8bd ~%u txs  diff %s#", t.reward_btc,
                              1u << (t.branches > 15 ? 15 : t.branches), nd);
        // pool tag, then the solo payout check: green when a block pays you, red when it doesn't
        char tg[96];
        const char *sep = t.tag[0] ? "  -  " : "";  // the screen font has no middle dot
        if (t.payout == Payout::PaysYou)
            snprintf(tg, sizeof(tg), "%s%spays you %.0f%%", t.tag, sep,
                     t.total_sats ? 100.0 * t.payout_sats / t.total_sats : 0.0);
        else if (t.payout == Payout::NotYou)
            snprintf(tg, sizeof(tg), "%s%sNOT your address", t.tag, sep);
        else
            snprintf(tg, sizeof(tg), "%s", t.tag);
        lv_label_set_text(s_now_tag, tg);
        lv_obj_set_style_text_color(s_now_tag, t.payout == Payout::NotYou ? C_RED : t.payout == Payout::PaysYou ? C_GREEN : C_MAGENTA, 0);
        lv_label_set_text_fmt(s_now_prev, "prev ...%s", t.prev_hash + 40);
    }

    // leading zeros glow; that's literally the proof of work
    if (s.last_hash[0]) {
        // 4 rows of 16; LVGL recolor doesn't carry across lines, so reopen it
        char buf[200];
        int o = 0, z = 0;
        while (z < 64 && s.last_hash[z] == '0') z++;
        bool colored = false;
        for (int i = 0; i < 64; i++) {
            if (i && i % 16 == 0) {
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
        lv_label_set_text(s_live_hash, buf);
        char d[24];
        fmt_diff(s.last_hash_diff, d, sizeof(d));
        lv_label_set_text_fmt(s_live_diff, "diff %s   %lu checked", d, (unsigned long) s.hashes_checked);
    }
}
