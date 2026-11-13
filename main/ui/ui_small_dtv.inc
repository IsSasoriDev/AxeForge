// DTV theme on the 320x170 screen: its own pages, not the Forge pages recoloured.
// An orange rail on the left names the page; the logo replaces the AxeForge text;
// panels have cut corners; the DTV crate and coin from the logo are the shapes:
// the hashrate fills a stack of crates, mined blocks and chips are crates.
// Included by ui_small.cpp, which owns page order, buttons and the header state.

#define D_OR 0xff8000
#define D_EMB 0xc2570a
#define D_LIT 0xffa04a
#define D_HOT 0xffc080
#define D_TXT 0xf2f2f2
#define D_DIM 0x8c8c8c
#define D_RULE 0x3a2414
#define D_INK 0x140800
#define D_RED 0xf0434f
#define D_RAIL 25
#define D_X0 32         // content left
#define D_Y0 27         // content top, under the header rule
#define D_PW (W - D_X0) // 288
#define D_PH (H - D_Y0) // 143

LV_IMG_DECLARE(dtv_logo_22);
LV_IMG_DECLARE(dtv_logo_56);
LV_FONT_DECLARE(lv_font_montserrat_48);

// ------------------------------------------------------------------ shapes
// Drawn in the object's own area by one draw callback; the parameters live in a
// small pool that is reset on every build (a theme change rebuilds the screen).
enum class Sh : uint8_t { Crate, Wire, Cham, Coin, Ticks, Ticket, Diamond };
struct Shape {
    Sh kind;
    uint8_t n;       // ticks lit; cham: split line; crate: lit
    uint8_t depth;   // crate: side height
    uint32_t c1;     // crate top / outline colour
    uint32_t fill;   // cham: fill colour
    lv_opa_t opa;    // cham: fill opacity
};
static Shape s_shp[64];
static int s_nshp = 0;

static void line(lv_draw_ctx_t *ctx, lv_draw_line_dsc_t *d, int x1, int y1, int x2, int y2)
{
    lv_point_t a = {(lv_coord_t) x1, (lv_coord_t) y1}, b = {(lv_coord_t) x2, (lv_coord_t) y2};
    lv_draw_line(ctx, d, &a, &b);
}

static void poly(lv_draw_ctx_t *ctx, uint32_t col, lv_opa_t opa, const lv_point_t *p, int n)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(col);
    d.bg_opa = opa;
    lv_draw_polygon(ctx, &d, p, n);
}

static void shape_draw_cb(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target(e);
    Shape *s = (Shape *) lv_obj_get_user_data(o);
    if (!s) return;
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    const int x = a.x1, y = a.y1, w = lv_area_get_width(&a), h = lv_area_get_height(&a);
    lv_draw_line_dsc_t ld;
    lv_draw_line_dsc_init(&ld);
    ld.width = 1;
    ld.color = lv_color_hex(s->c1);

    switch (s->kind) {
    case Sh::Crate:
    case Sh::Wire: {
        // isometric box: top rhombus, left and right faces
        const int b = (w * 28 + 50) / 100, d = s->depth, cx = x + w / 2, ty = y + b, r = x + w - 1;
        lv_point_t top[4] = {{(lv_coord_t) x, (lv_coord_t) ty}, {(lv_coord_t) cx, (lv_coord_t) y},
                             {(lv_coord_t) r, (lv_coord_t) ty}, {(lv_coord_t) cx, (lv_coord_t) (ty + b)}};
        lv_point_t lf[4] = {{(lv_coord_t) x, (lv_coord_t) ty}, {(lv_coord_t) cx, (lv_coord_t) (ty + b)},
                            {(lv_coord_t) cx, (lv_coord_t) (ty + b + d)}, {(lv_coord_t) x, (lv_coord_t) (ty + d)}};
        lv_point_t rf[4] = {{(lv_coord_t) cx, (lv_coord_t) (ty + b)}, {(lv_coord_t) r, (lv_coord_t) ty},
                            {(lv_coord_t) r, (lv_coord_t) (ty + d)}, {(lv_coord_t) cx, (lv_coord_t) (ty + b + d)}};
        if (s->kind == Sh::Crate && s->n) {
            poly(ctx, D_OR, LV_OPA_COVER, lf, 4);
            poly(ctx, D_EMB, LV_OPA_COVER, rf, 4);
            poly(ctx, s->c1, LV_OPA_COVER, top, 4);
            break;
        }
        if (s->kind == Sh::Crate) {  // empty crate: dark with a brown edge
            poly(ctx, 0x101010, LV_OPA_COVER, lf, 4);
            poly(ctx, 0x0b0b0b, LV_OPA_COVER, rf, 4);
            poly(ctx, 0x161616, LV_OPA_COVER, top, 4);
            ld.color = lv_color_hex(D_RULE);
        } else {
            ld.opa = LV_OPA_80;
        }
        for (int i = 0; i < 4; i++) line(ctx, &ld, top[i].x, top[i].y, top[(i + 1) % 4].x, top[(i + 1) % 4].y);
        line(ctx, &ld, x, ty, x, ty + d);
        line(ctx, &ld, r, ty, r, ty + d);
        line(ctx, &ld, cx, ty + b, cx, ty + b + d);
        line(ctx, &ld, x, ty + d, cx, ty + b + d);
        line(ctx, &ld, cx, ty + b + d, r, ty + d);
        break;
    }
    case Sh::Cham: {
        // panel with the top-left and bottom-right corners cut off
        const int c = 6, r = x + w - 1, bt = y + h - 1;
        lv_point_t p[6] = {{(lv_coord_t) (x + c), (lv_coord_t) y}, {(lv_coord_t) r, (lv_coord_t) y},
                           {(lv_coord_t) r, (lv_coord_t) (bt - c)}, {(lv_coord_t) (r - c), (lv_coord_t) bt},
                           {(lv_coord_t) x, (lv_coord_t) bt}, {(lv_coord_t) x, (lv_coord_t) (y + c)}};
        poly(ctx, s->fill, s->opa, p, 6);
        for (int i = 0; i < 6; i++) line(ctx, &ld, p[i].x, p[i].y, p[(i + 1) % 6].x, p[(i + 1) % 6].y);
        if (s->n) line(ctx, &ld, x + 2, y + h / 2, r - 2, y + h / 2);  // split-flap line
        break;
    }
    case Sh::Coin: {
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.radius = LV_RADIUS_CIRCLE;
        d.bg_color = lv_color_hex(D_OR);
        lv_draw_rect(ctx, &d, &a);
        lv_area_t in = {(lv_coord_t) (a.x1 + 3), (lv_coord_t) (a.y1 + 3), (lv_coord_t) (a.x2 - 3), (lv_coord_t) (a.y2 - 3)};
        d.bg_opa = LV_OPA_TRANSP;
        d.border_color = lv_color_hex(0xffffff);
        d.border_width = 1;
        lv_draw_rect(ctx, &d, &in);
        // the two strokes through the B
        const int cx = x + w / 2, cy = y + h / 2, rr = w / 2;
        ld.color = lv_color_hex(0xffffff);
        for (int k = -1; k <= 1; k += 2) {
            line(ctx, &ld, cx - 2, cy + k * rr * 40 / 100, cx - 2, cy + k * rr * 62 / 100);
            line(ctx, &ld, cx + 1, cy + k * rr * 40 / 100, cx + 1, cy + k * rr * 62 / 100);
        }
        break;
    }
    case Sh::Ticks: {
        // 30 slanted ticks, one per two seconds
        const float step = (w - 4) / 30.0f;
        for (int i = 0; i < 30; i++) {
            int x1 = x + 3 + (int) (i * step);
            lv_point_t p[4] = {{(lv_coord_t) (x1 + 3), (lv_coord_t) y}, {(lv_coord_t) (x1 + 3 + (int) step - 3), (lv_coord_t) y},
                               {(lv_coord_t) (x1 + (int) step - 3), (lv_coord_t) (y + h - 1)}, {(lv_coord_t) x1, (lv_coord_t) (y + h - 1)}};
            poly(ctx, i < s->n ? D_OR : 0x1a1a1a, LV_OPA_COVER, p, 4);
        }
        break;
    }
    case Sh::Ticket: {
        // outline, a dotted tear line after the stub, a punched notch on the right
        const int r = x + w - 1, bt = y + h - 1, my = y + h / 2;
        lv_area_t f = a;
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.bg_color = lv_color_hex(0x0b0b0b);
        d.bg_opa = LV_OPA_80;
        d.border_color = lv_color_hex(s->c1);
        d.border_width = 1;
        lv_draw_rect(ctx, &d, &f);
        for (int yy = y + 3; yy < bt - 2; yy += 4) line(ctx, &ld, x + 32, yy, x + 32, yy + 1);
        lv_area_t n = {(lv_coord_t) (r - 4), (lv_coord_t) (my - 4), (lv_coord_t) (r + 4), (lv_coord_t) (my + 4)};
        d.radius = LV_RADIUS_CIRCLE;
        d.bg_color = lv_color_hex(0x000000);
        d.bg_opa = LV_OPA_COVER;
        lv_draw_rect(ctx, &d, &n);
        break;
    }
    case Sh::Diamond: {
        lv_point_t p[4] = {{(lv_coord_t) (x + w / 2), (lv_coord_t) y}, {(lv_coord_t) (x + w - 1), (lv_coord_t) (y + h / 2)},
                           {(lv_coord_t) (x + w / 2), (lv_coord_t) (y + h - 1)}, {(lv_coord_t) x, (lv_coord_t) (y + h / 2)}};
        poly(ctx, s->c1, LV_OPA_COVER, p, 4);
        break;
    }
    }
}

static lv_obj_t *shape(lv_obj_t *p, Sh kind, int x, int y, int w, int h, uint32_t c1)
{
    if (s_nshp >= (int) (sizeof(s_shp) / sizeof(s_shp[0]))) return box(p, x, y, w, h, lv_color_hex(c1), 0);
    Shape *s = &s_shp[s_nshp++];
    *s = {kind, 0, 0, c1, 0x0b0b0b, LV_OPA_80};
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(o, s);
    lv_obj_add_event_cb(o, shape_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    return o;
}

static Shape *shp(lv_obj_t *o) { return (Shape *) lv_obj_get_user_data(o); }

// a crate w wide whose sides are `depth` tall; returns the object (its height follows)
static lv_obj_t *crate(lv_obj_t *p, Sh kind, int cx, int y, int w, int depth)
{
    const int b = (w * 28 + 50) / 100;
    lv_obj_t *o = shape(p, kind, cx - w / 2, y, w, 2 * b + depth + 1, D_LIT);
    if (Shape *s = shp(o)) s->depth = (uint8_t) depth;
    return o;
}

static void set_crate(lv_obj_t *o, bool lit, uint32_t top)
{
    Shape *s = shp(o);
    if (!s || (s->n == lit && s->c1 == top)) return;
    s->n = lit;
    s->c1 = top;
    lv_obj_invalidate(o);
}

static lv_obj_t *cham(lv_obj_t *p, int x, int y, int w, int h) { return shape(p, Sh::Cham, x, y, w, h, D_OR); }

static void coin(lv_obj_t *p, int cx, int cy, int r)
{
    shape(p, Sh::Coin, cx - r, cy - r, 2 * r, 2 * r, D_OR);
    lv_obj_t *b = label(p, r >= 14 ? &lv_font_montserrat_20 : r >= 11 ? &lv_font_montserrat_14 : &lv_font_montserrat_12,
                        lv_color_hex(0xffffff), "B");
    lv_obj_update_layout(b);
    lv_obj_set_pos(b, cx - lv_obj_get_width(b) / 2, cy - lv_obj_get_height(b) / 2);
}

// a label centred on x in a box `w` wide
static lv_obj_t *clabel(lv_obj_t *p, const lv_font_t *f, uint32_t c, int cx, int y, int w)
{
    lv_obj_t *l = label(p, f, lv_color_hex(c), "");
    lv_label_set_recolor(l, true);
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(l, cx - w / 2, y);
    return l;
}

static lv_obj_t *dlabel(lv_obj_t *p, const lv_font_t *f, uint32_t c, int x, int y, const char *t = "")
{
    lv_obj_t *l = label(p, f, lv_color_hex(c), t);
    lv_label_set_recolor(l, true);
    lv_obj_set_pos(l, x, y);
    return l;
}

// right-aligned in a box ending at x2
static lv_obj_t *rlabel_at(lv_obj_t *p, const lv_font_t *f, uint32_t c, int x2, int y, int w)
{
    lv_obj_t *l = dlabel(p, f, c, x2 - w, y);
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    return l;
}

// ------------------------------------------------------------------ state
static lv_obj_t *d_rail_name, *d_rail_idx, *d_splash, *d_splash_bar;
static int d_splash_pct = 0;
// dashboard
static lv_obj_t *d_hash, *d_unit, *d_sub, *d_plate[4], *d_tune, *d_stack[5];
// clock
static lv_obj_t *d_digit[4], *d_ticks, *d_date, *d_wx, *d_city;
// blocks
static lv_obj_t *d_blk[4], *d_blk_l1[4], *d_blk_l2[4], *d_now, *d_best;
// pool
static lv_obj_t *d_ptot, *d_tix[AF_POOLS], *d_tix_proto[AF_POOLS], *d_tix_host[AF_POOLS], *d_tix_info[AF_POOLS];
// chips
static lv_obj_t *d_chip[AF_NUM_ASICS], *d_chip_ghs[AF_NUM_ASICS], *d_chip_t[AF_NUM_ASICS], *d_chip_bar[AF_NUM_ASICS];
// system
static lv_obj_t *d_qr, *d_key[5], *d_val[5], *d_ver;

static const char *const D_NAMES[NUM_KINDS] = {"H\nA\nS\nH", "T\nI\nM\nE", "B\nL\nO\nC\nK", "P\nO\nO\nL", "C\nH\nI\nP\nS",
                                               "S\nY\nS"};

static lv_obj_t *dtv_page(lv_obj_t *scr)
{
    lv_obj_t *p = lv_obj_create(scr);
    lv_obj_remove_style_all(p);
    lv_obj_set_pos(p, D_X0, D_Y0);
    lv_obj_set_size(p, D_PW, D_PH);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    return p;
}

// ------------------------------------------------------------------ build
static void dtv_build_frame(lv_obj_t *scr)
{
    lv_obj_t *rail = box(scr, 0, 0, D_RAIL, H, lv_color_hex(D_OR), 0);
    d_rail_name = label(rail, &lv_font_montserrat_12, lv_color_hex(D_INK), "");
    lv_obj_set_width(d_rail_name, D_RAIL);
    lv_obj_set_style_text_align(d_rail_name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(d_rail_name, -3, 0);
    lv_obj_align(d_rail_name, LV_ALIGN_LEFT_MID, 0, 0);
    d_rail_idx = label(rail, &lv_font_montserrat_14, lv_color_hex(D_INK), "01");
    lv_obj_set_width(d_rail_idx, D_RAIL);
    lv_obj_set_style_text_align(d_rail_idx, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(d_rail_idx, LV_ALIGN_BOTTOM_MID, 0, -3);
    shape(scr, Sh::Diamond, D_RAIL - 4, 4, 8, 8, D_INK);  // a notch at the rail's top

    lv_obj_t *logo = lv_img_create(scr);
    lv_img_set_src(logo, &dtv_logo_22);
    lv_obj_set_pos(logo, D_X0, 2);
    s_status_dot = box(scr, D_X0 + 72, 10, 6, 6, C_GREEN, 3);
    s_status = label(scr, &lv_font_montserrat_12, C_GREEN, "");
    lv_obj_set_pos(s_status, D_X0 + 82, 6);
    s_hdr_right = label(scr, &lv_font_montserrat_12, lv_color_hex(D_DIM), "");
    lv_obj_align(s_hdr_right, LV_ALIGN_TOP_RIGHT, -5, 6);
    box(scr, D_X0, 25, W - D_X0 - 4, 1, lv_color_hex(D_RULE), 0);
}

static void dtv_build_dash(lv_obj_t *p)
{
    dlabel(p, &lv_font_montserrat_12, D_OR, 2, 0, "HASHRATE");
    d_hash = dlabel(p, &lv_font_montserrat_48, D_TXT, 0, 12, "--");
    d_unit = dlabel(p, &lv_font_montserrat_20, D_OR, 120, 34, "TH/s");
    d_sub = dlabel(p, &lv_font_montserrat_12, D_DIM, 2, 64);
    static const char *caps[4] = {"CHIP", "VR", "POWER", "FAN"};
    static const int px[4] = {0, 52, 104, 166}, pw[4] = {48, 48, 58, 44};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *c = cham(p, px[i], 82, pw[i], 36);
        dlabel(c, &lv_font_montserrat_12, D_OR, 6, 2, caps[i]);
        d_plate[i] = dlabel(c, &lv_font_montserrat_14, D_TXT, 6, 17, "--");
    }
    d_tune = dlabel(p, &lv_font_montserrat_12, D_DIM, 2, 124);
    lv_obj_set_width(d_tune, 208);
    lv_label_set_long_mode(d_tune, LV_LABEL_LONG_SCROLL_CIRCULAR);
    // five crates, bottom first so each one sits on the one below
    const int cx = 250, w = 56, b = (w * 28 + 50) / 100, depth = 8, oh = 2 * b + depth + 1, bottom = D_PH - 2;
    for (int k = 0; k < 5; k++) d_stack[k] = crate(p, Sh::Crate, cx, bottom - oh - k * 16, w, depth);
    coin(p, cx, bottom - oh - 4 * 16 - 10, 12);
}

static void dtv_build_clock(lv_obj_t *p)
{
    static const int tx[4] = {2, 54, 118, 170};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *t = cham(p, tx[i], 4, 48, 66);
        shp(t)->n = 1;
        d_digit[i] = label(t, &lv_font_montserrat_48, lv_color_hex(D_TXT), "-");
        lv_obj_set_width(d_digit[i], 48);
        lv_obj_set_style_text_align(d_digit[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(d_digit[i], LV_ALIGN_CENTER, 0, 0);
    }
    shape(p, Sh::Diamond, 106, 22, 8, 8, D_OR);
    shape(p, Sh::Diamond, 106, 44, 8, 8, D_OR);
    coin(p, 256, 37, 16);
    d_ticks = shape(p, Sh::Ticks, 0, 78, 224, 7, D_OR);
    d_date = dlabel(p, &lv_font_montserrat_20, D_OR, 2, 94);
    d_wx = rlabel_at(p, &lv_font_montserrat_14, D_TXT, D_PW - 4, 96, 150);
    d_city = rlabel_at(p, &lv_font_montserrat_12, D_DIM, D_PW - 4, 116, 150);
}

static void dtv_build_blocks(lv_obj_t *p)
{
    static const int cx[4] = {28, 83, 136, 191};
    dlabel(p, &lv_font_montserrat_12, D_DIM, 6, 0, "coming");
    dlabel(p, &lv_font_montserrat_12, D_OR, 156, 0, "mined");
    for (int y = 14; y < 70; y += 6) box(p, 108, y, 2, 3, lv_color_hex(D_OR), 0);
    for (int i = 0; i < 4; i++) {
        d_blk[i] = crate(p, i < 2 ? Sh::Wire : Sh::Crate, cx[i], 24, 46, 20);
        if (i >= 2) set_crate(d_blk[i], true, i == 2 ? D_HOT : D_LIT);
        d_blk_l1[i] = clabel(p, &lv_font_montserrat_14, i < 2 ? D_DIM : D_TXT, cx[i], 74, 54);
        d_blk_l2[i] = clabel(p, &lv_font_montserrat_12, D_DIM, cx[i], 91, 54);
    }
    coin(p, cx[2], 12, 9);
    dlabel(p, &lv_font_montserrat_12, D_DIM, 226, 24, "best");
    d_best = dlabel(p, &lv_font_montserrat_20, D_TXT, 226, 40, "--");
    lv_obj_t *n = cham(p, 0, 108, D_PW - 2, 34);
    dlabel(n, &lv_font_montserrat_12, D_OR, 8, 2, "NOW MINING");
    d_now = dlabel(n, &lv_font_montserrat_12, D_TXT, 8, 17);
    lv_obj_set_width(d_now, D_PW - 20);
    lv_label_set_long_mode(d_now, LV_LABEL_LONG_DOT);
}

static void dtv_build_pool(lv_obj_t *p)
{
    d_ptot = dlabel(p, &lv_font_montserrat_20, D_TXT, 0, 0, "--");
    dlabel(p, &lv_font_montserrat_12, D_DIM, 0, 24, "accepted / rejected / best");
    for (int i = 0; i < AF_POOLS; i++) {
        const int y = 42 + i * 25;
        d_tix[i] = shape(p, Sh::Ticket, 0, y, D_PW - 6, 23, D_RULE);
        d_tix_proto[i] = clabel(p, &lv_font_montserrat_12, D_DIM, 16, y + 5, 30);
        d_tix_host[i] = dlabel(p, &lv_font_montserrat_12, D_TXT, 40, y + 5);
        lv_obj_set_width(d_tix_host[i], 120);
        lv_label_set_long_mode(d_tix_host[i], LV_LABEL_LONG_DOT);
        d_tix_info[i] = rlabel_at(p, &lv_font_montserrat_12, D_DIM, D_PW - 16, y + 5, 110);
    }
}

static void dtv_build_chips(lv_obj_t *p)
{
    const int n = AF_NUM_ASICS, per = n < 4 ? n : 4, rows = (n + 3) / 4, cw = D_PW / per;
    for (int i = 0; i < n; i++) {
        const int cx = (i % per) * cw + cw / 2, y0 = (i / per) * 71;
        if (rows == 1) {
            const int w = cw * 55 / 100 < 64 ? cw * 55 / 100 : 64;
            d_chip_ghs[i] = clabel(p, &lv_font_montserrat_20, D_TXT, cx, 0, cw);
            clabel(p, &lv_font_montserrat_12, D_DIM, cx, 22, cw);
            lv_label_set_text(lv_obj_get_child(p, lv_obj_get_child_cnt(p) - 1), "GH/s");
            d_chip[i] = crate(p, Sh::Crate, cx, 42, w, 14);
            d_chip_t[i] = clabel(p, &lv_font_montserrat_14, D_TXT, cx, 92, cw);
            lv_obj_t *cap = clabel(p, &lv_font_montserrat_12, D_OR, cx, 110, cw);
            lv_label_set_text_fmt(cap, "ASIC %d", i + 1);
            d_chip_bar[i] = bar(p, cw - 14, lv_color_hex(D_OR));
            lv_obj_set_height(d_chip_bar[i], 4);
            lv_obj_set_pos(d_chip_bar[i], cx - (cw - 14) / 2, 132);
        } else {
            d_chip_ghs[i] = clabel(p, &lv_font_montserrat_14, D_TXT, cx, y0, cw);
            d_chip[i] = crate(p, Sh::Crate, cx, y0 + 18, 26, 7);
            d_chip_t[i] = clabel(p, &lv_font_montserrat_12, D_DIM, cx, y0 + 44, cw);
            d_chip_bar[i] = bar(p, cw - 14, lv_color_hex(D_OR));
            lv_obj_set_pos(d_chip_bar[i], cx - (cw - 14) / 2, y0 + 62);
        }
        lv_obj_set_style_bg_color(d_chip_bar[i], lv_color_hex(0x161616), LV_PART_MAIN);
    }
}

static void dtv_build_system(lv_obj_t *p)
{
    lv_obj_t *f = cham(p, 0, 0, 120, 120);
    shp(f)->fill = D_OR;
    shp(f)->opa = LV_OPA_COVER;
    d_qr = lv_qrcode_create(p, 100, lv_color_black(), lv_color_white());
    lv_obj_set_pos(d_qr, 10, 10);
    clabel(p, &lv_font_montserrat_12, D_OR, 60, 126, 120);
    lv_label_set_text(lv_obj_get_child(p, lv_obj_get_child_cnt(p) - 1), "SCAN TO OPEN");
    for (int i = 0; i < 5; i++) {
        d_key[i] = dlabel(p, &lv_font_montserrat_12, D_DIM, 128, 2 + i * 16);
        d_val[i] = rlabel_at(p, &lv_font_montserrat_12, D_TXT, D_PW - 4, 2 + i * 16, 118);
    }
    lv_obj_t *plate = box(p, 128, 88, D_PW - 132, 20, lv_color_hex(D_OR), 0);
    lv_obj_t *pl = label(plate, &lv_font_montserrat_12, lv_color_hex(D_INK), "FIRST SUPPORTER");
    lv_obj_center(pl);
    d_ver = dlabel(p, &lv_font_montserrat_12, D_DIM, 128, 116);
}

static void splash_cb(lv_timer_t *t)
{
    d_splash_pct += 4;
    if (d_splash && d_splash_bar) lv_obj_set_width(d_splash_bar, d_splash_pct * 160 / 100);
    if (d_splash_pct >= 100) {
        if (d_splash) lv_obj_del(d_splash);
        d_splash = d_splash_bar = nullptr;
        lv_timer_del(t);
    }
}

// The boot screen: the logo and "first supporter" while the bar fills, ~2.5 s
static void dtv_build_splash(lv_obj_t *scr)
{
    d_splash = box(scr, 0, 0, W, H, lv_color_hex(0x000000), 0);
    lv_obj_t *logo = lv_img_create(d_splash);
    lv_img_set_src(logo, &dtv_logo_56);
    lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 32);
    lv_obj_t *fs = label(d_splash, &lv_font_montserrat_12, lv_color_hex(D_OR), "FIRST SUPPORTER");
    lv_obj_set_style_text_letter_space(fs, 3, 0);
    lv_obj_align(fs, LV_ALIGN_TOP_MID, 0, 98);
    lv_obj_t *track = box(d_splash, (W - 160) / 2, 122, 160, 4, lv_color_hex(0x161616), 2);
    d_splash_bar = box(track, 0, 0, 0, 4, lv_color_hex(D_OR), 2);
    lv_obj_t *by = label(d_splash, &lv_font_montserrat_12, lv_color_hex(D_DIM), "");
    lv_label_set_text_fmt(by, "powered by AxeForge %s", esp_app_get_description()->version);
    lv_obj_align(by, LV_ALIGN_TOP_MID, 0, 140);
    d_splash_pct = 0;
    lv_timer_create(splash_cb, 100, nullptr);
}

static void dtv_build(lv_obj_t *scr)
{
    s_nshp = 0;
    dtv_build_frame(scr);
    for (int i = 0; i < NUM_KINDS; i++) s_pages[i] = dtv_page(scr);
    dtv_build_dash(s_pages[K_DASH]);
    dtv_build_clock(s_pages[K_CLOCK]);
    dtv_build_blocks(s_pages[K_BLOCKS]);
    dtv_build_pool(s_pages[K_POOL]);
    dtv_build_chips(s_pages[K_CHIPS]);
    dtv_build_system(s_pages[K_SYSTEM]);
    // page dots stay (load_order places them) but out of sight: the rail numbers pages
    lv_obj_t *hide = box(scr, 0, 0, 1, 1, lv_color_hex(0), 0);
    lv_obj_add_flag(hide, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < NUM_KINDS; i++) s_dots[i] = box(hide, 0, 0, 4, 4, C_TRACK, 2);
    s_banner = box(scr, D_RAIL, 26, W - D_RAIL, 20, C_RED, 0);
    lv_obj_set_style_bg_opa(s_banner, LV_OPA_90, 0);
    s_banner_lbl = label(s_banner, &lv_font_montserrat_12, lv_color_hex(0xffffff), "");
    lv_obj_center(s_banner_lbl);
    lv_obj_add_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
    dtv_build_splash(scr);
}

static void dtv_show_page()
{
    lv_label_set_text(d_rail_name, D_NAMES[s_order[s_page]]);
    lv_label_set_text_fmt(d_rail_idx, "%02d", s_page + 1);
}

// ------------------------------------------------------------------ refresh
static void dtv_refresh_dash()
{
    const MinerState &s = g_state;
    char v[16];
    const char *u;
    fmt_hash(s.hashrate_ghs, v, sizeof(v), &u);
    lv_label_set_text(d_hash, v);
    lv_label_set_text(d_unit, u);
    lv_obj_align_to(d_unit, d_hash, LV_ALIGN_OUT_RIGHT_BOTTOM, 6, -8);
    float pct = s.expected_ghs > 1 ? 100.0f * s.hashrate_ghs / s.expected_ghs : 0;
    char ev[16];
    const char *eu;
    fmt_hash(s.expected_ghs, ev, sizeof(ev), &eu);
    lv_label_set_text_fmt(d_sub, "%.0f%% of %s %s   err %.2f%%", pct, ev, eu, s.error_pct);
    // one crate per 20 % of expected; the top lit one is the brightest
    int lit = (int) ((pct + 10) / 20);
    if (lit > 5) lit = 5;
    if (lit < 0) lit = 0;
    for (int k = 0; k < 5; k++) set_crate(d_stack[k], k < lit, k == lit - 1 ? D_HOT : D_LIT);
    lv_label_set_text_fmt(d_plate[0], "%.0f\xC2\xB0", s.chip_temp_max);
    lv_obj_set_style_text_color(d_plate[0], lv_color_hex(s.chip_temp_max >= CONFIG_AF_MAX_CHIP_TEMP ? D_RED
                                                         : s.chip_temp_max >= CONFIG_AF_MAX_CHIP_TEMP - 6 ? D_LIT
                                                                                                          : D_TXT), 0);
    if (AF_HAS_VR_SENSOR) lv_label_set_text_fmt(d_plate[1], "%.0f\xC2\xB0", s.vr_temp);
    else lv_label_set_text(d_plate[1], "--");
    lv_label_set_text_fmt(d_plate[2], "%.1fW", s.pin);
    lv_label_set_text_fmt(d_plate[3], "%u%%", s.fan_pct);
    const char *ph = s.tuner.phase == TunerPhase::Locked ? "LOCKED" : s.tuner.phase == TunerPhase::Off ? "MANUAL" : "TUNING";
    lv_label_set_text_fmt(d_tune, "#ff8000 %s#  %u MHz  %u.%02u V  %s", ph, s.freq_mhz, s.core_mv / 1000,
                          (s.core_mv % 1000) / 10, s.tuner.note);
}

static void dtv_refresh_clock()
{
    static const char *wd[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char *mo[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    struct tm t;
    Shape *tk = shp(d_ticks);
    if (weather::local_time(&t)) {
        int h = t.tm_hour;
        if (!settings::get().clock_24h) h = h % 12 ? h % 12 : 12;
        const int dg[4] = {h / 10, h % 10, t.tm_min / 10, t.tm_min % 10};
        for (int i = 0; i < 4; i++) lv_label_set_text_fmt(d_digit[i], "%d", dg[i]);
        if (tk && tk->n != (t.tm_sec + 1) / 2) {
            tk->n = (uint8_t) ((t.tm_sec + 1) / 2);
            lv_obj_invalidate(d_ticks);
        }
        lv_label_set_text_fmt(d_date, "%s %d %s", wd[t.tm_wday], t.tm_mday, mo[t.tm_mon]);
    } else {
        for (int i = 0; i < 4; i++) lv_label_set_text(d_digit[i], "-");
        lv_label_set_text(d_date, "SYNCING TIME");
    }
    const WeatherState &w = g_state.weather;
    if (w.valid) {
        lv_label_set_text_fmt(d_wx, "%.0f\xC2\xB0 %s", w.temp, weather::describe(w.code));
        lv_label_set_text(d_city, w.city);
    } else {
        lv_label_set_text(d_wx, "");
        lv_label_set_text(d_city, w.error[0] ? w.error : "no weather");
    }
}

static void dtv_refresh_blocks()
{
    const MinerState &s = g_state;
    const MempoolState &m = s.mempool;
    time_t now_t = time(nullptr);
    for (int i = 0; i < 4; i++) {
        if (!m.valid) {
            lv_label_set_text(d_blk_l1[i], "--");
            lv_label_set_text(d_blk_l2[i], "");
            continue;
        }
        if (i < 2) {  // the second block to come on the left, the next one by the divider
            const MempoolNext &nb = m.next[1 - i];
            lv_label_set_text_fmt(d_blk_l1[i], "~%.0f sat", nb.median_fee);
            lv_label_set_text(d_blk_l2[i], i == 1 ? "next" : "after");
        } else {
            const MempoolBlock &b = m.mined[i - 2];
            lv_label_set_text_fmt(d_blk_l1[i], "%lu", (unsigned long) b.height);
            // the cells are 55 px apart: minutes only, the pool name does not fit
            if (b.time && now_t > 1600000000) lv_label_set_text_fmt(d_blk_l2[i], "%ld min", (long) ((now_t - b.time) / 60));
            else lv_label_set_text_fmt(d_blk_l2[i], "%.7s", b.pool);
        }
    }
    char best[16];
    fmt_diff(s.best_diff, best, sizeof(best));
    lv_label_set_text(d_best, best);
    const BlockTemplate &t = s.tmpl;
    if (t.valid) {
        const char *pay = t.payout == Payout::PaysYou ? "#00cc88 pays you#" : t.payout == Payout::NotYou ? "#f0434f NOT you#" : t.tag;
        lv_label_set_text_fmt(d_now, "%lu   #ff8000 %.3f BTC#   %s", (unsigned long) t.height, t.reward_btc, pay);
    }
}

static void dtv_refresh_pool()
{
    const MinerState &s = g_state;
    char best[16];
    fmt_diff(s.best_diff, best, sizeof(best));
    lv_label_set_text_fmt(d_ptot, "%lu #ff8000 /# %lu #ff8000 /# %s", (unsigned long) s.shares_accepted,
                          (unsigned long) s.shares_rejected, best);
    static const char *proto[] = {"SV1", "TLS", "SV2"};
    for (int i = 0; i < AF_POOLS; i++) {
        const PoolState &p = s.pools[i];
        const bool live = p.enabled && p.connected && p.work_pct > 0;
        Shape *t = shp(d_tix[i]);
        if (t && t->c1 != (live ? (uint32_t) D_OR : (uint32_t) D_RULE)) {
            t->c1 = live ? D_OR : D_RULE;
            lv_obj_invalidate(d_tix[i]);
        }
        lv_obj_set_style_text_color(d_tix_proto[i], lv_color_hex(live ? D_OR : D_DIM), 0);
        lv_obj_set_style_text_color(d_tix_host[i], lv_color_hex(live ? D_TXT : D_DIM), 0);
        if (!p.enabled) {
            lv_label_set_text_fmt(d_tix_proto[i], "%d", i + 1);
            lv_label_set_text(d_tix_host[i], "not set up");
            lv_label_set_text(d_tix_info[i], "");
            continue;
        }
        lv_label_set_text(d_tix_proto[i], proto[p.proto < 3 ? p.proto : 0]);
        lv_label_set_text(d_tix_host[i], p.host);
        if (p.connected)
            lv_label_set_text_fmt(d_tix_info[i], "%u%%  %lu/%lu", p.work_pct, (unsigned long) p.accepted, (unsigned long) p.rejected);
        else lv_label_set_text(d_tix_info[i], "connecting");
    }
}

static void dtv_refresh_chips()
{
    const MinerState &s = g_state;
    float per = s.expected_ghs / AF_NUM_ASICS, best = 1;
    for (int i = 0; i < AF_NUM_ASICS; i++)
        if (s.chip_ghs[i] > best) best = s.chip_ghs[i];
    for (int i = 0; i < AF_NUM_ASICS; i++) {
        lv_label_set_text_fmt(d_chip_ghs[i], "%.0f", s.chip_ghs[i]);
        const float t = s.chip_temp[i];
        lv_label_set_text_fmt(d_chip_t[i], "%s%.0f\xC2\xB0" "C", s.chip_temp_own[i] ? "" : "~", t);
        uint32_t top = t >= CONFIG_AF_MAX_CHIP_TEMP ? D_RED : t >= CONFIG_AF_MAX_CHIP_TEMP - 6 ? D_HOT : D_LIT;
        set_crate(d_chip[i], s.chip_ghs[i] > 1, top);
        lv_bar_set_value(d_chip_bar[i], (int) (100 * s.chip_ghs[i] / (per > best ? per : best)), LV_ANIM_ON);
    }
}

static void dtv_refresh_system()
{
    const MinerState &s = g_state;
    char qr[96];
    static const char *k_run[5] = {"host", "ip", "wifi", "uptime", "chips"};
    static const char *k_ap[5] = {"SETUP", "1. join", "password", "2. open", ""};
    const bool ap = s.ap_ssid[0] && !s.ip[0];
    for (int i = 0; i < 5; i++) lv_label_set_text(d_key[i], ap ? k_ap[i] : k_run[i]);
    if (ap) {
        if (s.ap_pass[0]) snprintf(qr, sizeof(qr), "WIFI:T:WPA;S:%s;P:%s;;", s.ap_ssid, s.ap_pass);
        else snprintf(qr, sizeof(qr), "WIFI:T:nopass;S:%s;;", s.ap_ssid);
        lv_label_set_text(d_val[0], "");
        lv_label_set_text(d_val[1], s.ap_ssid);
        lv_label_set_text(d_val[2], s.ap_pass[0] ? s.ap_pass : "none");
        lv_label_set_text(d_val[3], "192.168.4.1");
        lv_label_set_text(d_val[4], "");
    } else {
        snprintf(qr, sizeof(qr), "http://%s/", s.ip);
        uint32_t u = (uint32_t) (esp_timer_get_time() / 1000000);
        lv_label_set_text(d_val[0], s.hostname);
        lv_label_set_text(d_val[1], s.ip);
        lv_label_set_text(d_val[2], s.ssid);
        lv_label_set_text_fmt(d_val[3], "%lud %02luh %02lum", (unsigned long) (u / 86400), (unsigned long) (u % 86400 / 3600),
                              (unsigned long) (u % 3600 / 60));
        lv_label_set_text_fmt(d_val[4], "%d x %s", s.chips_detected, asic::MODEL);
    }
    lv_label_set_text_fmt(d_ver, "AxeForge %s", esp_app_get_description()->version);
    if (strcmp(qr, s_qr_text)) {
        strcpy(s_qr_text, qr);
        lv_qrcode_update(d_qr, s_qr_text, strlen(s_qr_text));
    }
}

static void dtv_refresh(int kind)
{
    switch (kind) {
    case K_DASH: dtv_refresh_dash(); break;
    case K_CLOCK: dtv_refresh_clock(); break;
    case K_BLOCKS: dtv_refresh_blocks(); break;
    case K_POOL: dtv_refresh_pool(); break;
    case K_CHIPS: dtv_refresh_chips(); break;
    case K_SYSTEM: dtv_refresh_system(); break;
    }
}
