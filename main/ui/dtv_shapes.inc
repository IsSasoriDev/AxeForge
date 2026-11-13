// DTV theme drawing shared by both screens (ui.cpp, ui_small.cpp): colours, and
// the crate, coin, cut-corner panel, ticket, tick and diamond shapes, each drawn
// by one LVGL draw callback in its object's area. The host file provides label().

#ifndef DTV_SHAPES_INC
#define DTV_SHAPES_INC

#define D_OR 0xff8000
#define D_EMB 0xc2570a
#define D_LIT 0xffa04a
#define D_HOT 0xffc080
#define D_TXT 0xf2f2f2
#define D_DIM 0x8c8c8c
#define D_RULE 0x3a2414
#define D_INK 0x140800
#define D_RED 0xf0434f

LV_IMG_DECLARE(dtv_logo_22);
LV_IMG_DECLARE(dtv_logo_56);
LV_FONT_DECLARE(lv_font_montserrat_48);
LV_FONT_DECLARE(lv_font_montserrat_28);
LV_IMG_DECLARE(dtv_logo_26);

static lv_obj_t *d_box(lv_obj_t *p, int x, int y, int w, int h, lv_color_t c, int r)
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
        const int stub = s->depth ? s->depth : 32;  // depth holds the stub width
        for (int yy = y + 3; yy < bt - 2; yy += 4) line(ctx, &ld, x + stub, yy, x + stub, yy + 1);
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
    if (s_nshp >= (int) (sizeof(s_shp) / sizeof(s_shp[0]))) return d_box(p, x, y, w, h, lv_color_hex(c1), 0);
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
    lv_obj_t *b = label(p, r >= 22 ? &lv_font_montserrat_28 : r >= 14 ? &lv_font_montserrat_20 : r >= 11 ? &lv_font_montserrat_14 : &lv_font_montserrat_12,
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

#endif
