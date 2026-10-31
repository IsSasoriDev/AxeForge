#define THEME_IMPL
#include "theme.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "core/settings.h"

// Paper: ink on off-white, hairline ink rules, one red for faults, the way an
// e-paper panel would draw it. Known Forge colours map explicitly; anything
// else (fee colours, weather art) maps by brightness.
#define PAPER_BG 0xf3f1ea
#define PAPER_CARD 0xfbfaf5
#define PAPER_SOFT 0xe6e2d8
#define PAPER_RULE 0x1b1b1b
#define PAPER_INK 0x141414
#define PAPER_DIM 0x5e5e5a
#define PAPER_RED 0xb3261e
#define PAPER_TRACK 0xd6d2c7

struct Pair {
    uint32_t from, to;
};

// Ember: near-black neutrals with Bitcoin orange as the one accent (the palette of
// the solo-pool-rs dashboard). Forge colours map one to one; anything not listed
// stays as it is. Secondary text is a touch lighter than the dashboard's muted grey
// so it stays readable on the small T-Display.
#define EMBER_BG 0x0a0a0b
#define EMBER_CARD 0x131316
#define EMBER_SOFT 0x1a1a1f
#define EMBER_RULE 0x232328
#define EMBER_TEXT 0xededef
#define EMBER_DIM 0x9a9aa3
#define EMBER_ORANGE 0xf7931a
#define EMBER_AMBER 0xffb35c
#define EMBER_OK 0x3ecf8e
#define EMBER_WARN 0xe3b341
#define EMBER_BAD 0xf0564a
#define EMBER_COOL 0x9aa4ff

static const Pair EMBER_MAP[] = {
    {0x070a10, EMBER_BG},     {0x0b1220, EMBER_BG},    {0x0a0f18, EMBER_BG},    {0x05070b, EMBER_BG},
    {0x0f1520, EMBER_CARD},   {0x0b0f17, EMBER_CARD},  {0x141c2a, EMBER_SOFT},  {0x1c2635, EMBER_RULE}, {0x1c2636, EMBER_RULE},
    {0xe6edf6, EMBER_TEXT},   {0xe8ecf5, EMBER_TEXT},  {0x9aa8bd, EMBER_DIM},   {0x6b7a90, EMBER_DIM},  {0x8d99ab, EMBER_DIM},
    {0xc9d3e0, 0xc4c4cb},     {0x9fb4ff, 0xc4c4cb},
    {0x00e5ff, EMBER_ORANGE}, {0xff2bd6, EMBER_AMBER}, {0x3da5ff, EMBER_AMBER}, {0x8b5cff, EMBER_COOL},
    {0x2bff88, EMBER_OK},     {0xff9f1c, EMBER_WARN},  {0xffc83d, EMBER_WARN},  {0xff3b5c, EMBER_BAD},
};

// Spatial: deep space. Near-black indigo, starlight text, and the colours of a
// nebula for accents: sky blue, violet, lilac, a gas-giant amber, aurora green.
#define SPATIAL_BG 0x03040b
#define SPATIAL_BG2 0x0a0c24
#define SPATIAL_CARD 0x0c1029
#define SPATIAL_SOFT 0x161b3d
#define SPATIAL_RULE 0x252b5a
#define SPATIAL_TEXT 0xe9ecff
#define SPATIAL_DIM 0x9aa2c9
#define SPATIAL_SKY 0x7dd3fc
#define SPATIAL_LILAC 0xc084fc
#define SPATIAL_VIOLET 0xa78bfa
#define SPATIAL_INDIGO 0x818cf8
#define SPATIAL_GREEN 0x6ee7b7
#define SPATIAL_AMBER 0xfbbf72
#define SPATIAL_GOLD 0xfde68a
#define SPATIAL_RED 0xfb7185

static const Pair SPATIAL_MAP[] = {
    {0x070a10, SPATIAL_BG},   {0x0b1220, SPATIAL_BG2},  {0x0a0f18, SPATIAL_BG},    {0x05070b, SPATIAL_BG},
    {0x0f1520, SPATIAL_CARD}, {0x0b0f17, SPATIAL_CARD}, {0x141c2a, SPATIAL_SOFT},  {0x1c2635, SPATIAL_RULE},
    {0x1c2636, SPATIAL_RULE}, {0xe6edf6, SPATIAL_TEXT}, {0xe8ecf5, SPATIAL_TEXT},  {0x9aa8bd, SPATIAL_DIM},
    {0x6b7a90, SPATIAL_DIM},  {0x8d99ab, SPATIAL_DIM},  {0xc9d3e0, 0xc7cbef},      {0x9fb4ff, 0xa5b4fc},
    {0x00e5ff, SPATIAL_SKY},  {0xff2bd6, SPATIAL_LILAC}, {0x3da5ff, SPATIAL_INDIGO}, {0x8b5cff, SPATIAL_VIOLET},
    {0x2bff88, SPATIAL_GREEN}, {0xff9f1c, SPATIAL_AMBER}, {0xffc83d, SPATIAL_GOLD}, {0xff3b5c, SPATIAL_RED},
};

static const Pair PAPER_MAP[] = {
    {0x070a10, PAPER_BG},   {0x0b1220, PAPER_BG},   {0x0a0f18, PAPER_BG},   {0x05070b, PAPER_BG},
    {0x0f1520, PAPER_CARD}, {0x0b0f17, PAPER_CARD}, {0x141c2a, PAPER_SOFT}, {0x1c2635, PAPER_RULE}, {0x1c2636, PAPER_TRACK},
    {0xe6edf6, PAPER_INK},  {0x9aa8bd, PAPER_DIM},  {0x6b7a90, PAPER_DIM},  {0x00e5ff, PAPER_INK},
    {0xff2bd6, PAPER_INK},  {0x2bff88, PAPER_INK},  {0xff9f1c, PAPER_INK},  {0x8b5cff, PAPER_INK},
    {0xff3b5c, PAPER_RED},  {0xffc83d, PAPER_INK},  {0xc9d3e0, PAPER_DIM},  {0x3da5ff, PAPER_INK},
    {0xe8ecf5, PAPER_INK},  {0x9fb4ff, PAPER_INK},  {0x8d99ab, PAPER_DIM},
    // text drawn on an accent fill (tuner pill): accents become ink, so this becomes paper
    {0x000000, PAPER_BG},
};

// mempool.space's fee scale (defaultMempoolFeeColors at feeLevels in the
// mempool/mempool frontend): Ember draws block cubes the way mempool.space does.
static const uint16_t MEMPOOL_FEE_LEVELS[] = {0, 1, 2, 3, 4, 5, 6, 8, 10, 12, 15, 20, 30, 40, 50, 60, 70, 80, 90, 100, 125, 150, 175, 200, 250, 300, 350, 400, 500, 600, 700, 800, 900, 1000, 1200, 1400, 1600, 1800, 2000};
static const uint32_t MEMPOOL_FEE_COLORS[] = {
    0x007d3d, 0x557d00, 0x5d7d01, 0x637d02, 0x6d7d04, 0x757d05, 0x7d7d06, 0x867d08,
    0x8c7d09, 0x957d0b, 0x9b7d0c, 0xa67d0e, 0xaa7d0f, 0xb27d10, 0xbb7d11, 0xbf7d12,
    0xbf7815, 0xbf7319, 0xbe6c1e, 0xbe6820, 0xbd6125, 0xbd5c28, 0xbc552d, 0xbc4f30,
    0xbc4a34, 0xbb4339, 0xbb3d3c, 0xbb373f, 0xba3243, 0xb92b48, 0xb9254b, 0xb8214d,
    0xb71d4f, 0xb61951, 0xb41453, 0xb30e55, 0xb10857, 0xb00259, 0xae005b,
};

namespace theme {

uint32_t mempool_fee_rgb(float fee)
{
    int i = 0;
    for (int k = 0; k < (int) (sizeof(MEMPOOL_FEE_LEVELS) / sizeof(MEMPOOL_FEE_LEVELS[0])); k++)
        if (fee >= MEMPOOL_FEE_LEVELS[k]) i = k;
    return MEMPOOL_FEE_COLORS[i];
}

// Read live: a theme change rebuilds the screen (display::rebuild_ui) rather
// than rebooting, so every colour lookup during the rebuild must see the new one.
Id current()
{
    uint8_t s = settings::get().theme;
    return s == PAPER ? PAPER : s == EMBER ? EMBER : s == SPATIAL ? SPATIAL : FORGE;
}

bool paper() { return current() == PAPER; }
bool ember() { return current() == EMBER; }
bool spatial() { return current() == SPATIAL; }

uint32_t map(uint32_t rgb)
{
    rgb &= 0xffffff;
    if (ember()) {
        for (const Pair &p : EMBER_MAP)
            if (p.from == rgb) return p.to;
        return rgb;
    }
    if (spatial()) {
        for (const Pair &p : SPATIAL_MAP)
            if (p.from == rgb) return p.to;
        return rgb;
    }
    if (!paper()) return rgb;
    for (const Pair &p : PAPER_MAP)
        if (p.from == rgb) return p.to;
    if (rgb == 0xffffff) return 0xffffff;  // white text on the red banner stays white
    float lum = (0.2126f * ((rgb >> 16) & 0xff) + 0.7152f * ((rgb >> 8) & 0xff) + 0.0722f * (rgb & 0xff)) / 255.0f;
    if (lum < 0.18f) return PAPER_CARD;  // dark fills become paper
    if (lum > 0.55f) return PAPER_INK;   // bright marks become ink
    return PAPER_DIM;
}

lv_color_t color(uint32_t rgb) { return lv_color_hex(map(rgb)); }

// Rewrites inline recolor codes ("#rrggbb text#") through the palette.
static void remap_codes(char *s)
{
    static const char *hex = "0123456789abcdef";
    for (char *p = s; (p = strchr(p, '#')) != nullptr; p++) {
        uint32_t v = 0;
        int i = 1;
        for (; i <= 6; i++) {
            char c = p[i] | 0x20;
            const char *h = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ? strchr(hex, c) : nullptr;
            if (!h) break;
            v = (v << 4) | (uint32_t) (h - hex);
        }
        if (i != 7 || p[7] != ' ') continue;
        uint32_t m = map(v);
        for (int k = 6; k >= 1; k--, m >>= 4) p[k] = hex[m & 0xf];
        p += 7;
    }
}

void set_text(lv_obj_t *label, const char *text)
{
    if (current() == FORGE || !text || !strchr(text, '#')) {
        lv_label_set_text(label, text);
        return;
    }
    char buf[640];
    snprintf(buf, sizeof(buf), "%s", text);
    remap_codes(buf);
    lv_label_set_text(label, buf);
}

void set_text_fmt(lv_obj_t *label, const char *fmt, ...)
{
    char buf[640];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (current() != FORGE) remap_codes(buf);
    lv_label_set_text(label, buf);
}

lv_coord_t shadow(lv_coord_t width) { return paper() ? 0 : width; }

// A fixed starfield on the screen's background, drawn before the pages: about one
// star per 1500 pixels, a few brighter and bluer ones, the same sky every time.
static void starfield_cb(lv_event_t *e)
{
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_obj_t *scr = lv_event_get_target(e);
    lv_coord_t w = lv_obj_get_width(scr), h = lv_obj_get_height(scr);
    if (w <= 0 || h <= 0) return;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = LV_RADIUS_CIRCLE;
    uint32_t s = 0x5eed5a7u;
    auto next = [&]() { return s = s * 1664525u + 1013904223u; };
    const int stars = w * h / 1500;
    for (int i = 0; i < stars; i++) {
        lv_coord_t x = (lv_coord_t) ((next() >> 8) % w);
        lv_coord_t y = (lv_coord_t) ((next() >> 8) % h);
        uint32_t k = next() >> 28;  // 0..15
        lv_coord_t size = k >= 14 ? 2 : 1;
        d.bg_color = lv_color_hex(k >= 12 ? 0xc7d2fe : k >= 9 ? 0xfde68a : 0xffffff);
        d.bg_opa = (lv_opa_t) (70 + k * 11);
        lv_area_t a = {x, y, (lv_coord_t) (x + size - 1), (lv_coord_t) (y + size - 1)};
        lv_draw_rect(ctx, &d, &a);
    }
}

// Spatial's cards are glass: mostly see-through, so the stars show, with a thin
// starlight outline. LVGL can't blur what's behind, so it's clear glass, not frosted.
static void glassify(lv_obj_t *obj)
{
    const lv_color_t card = lv_color_hex(SPATIAL_CARD);
    uint32_t n = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(obj, i);
        if (lv_obj_get_style_bg_opa(c, LV_PART_MAIN) >= LV_OPA_80 &&
            lv_obj_get_style_bg_color(c, LV_PART_MAIN).full == card.full) {
            lv_obj_set_style_bg_opa(c, LV_OPA_20, LV_PART_MAIN);
            lv_obj_set_style_bg_grad_dir(c, LV_GRAD_DIR_NONE, LV_PART_MAIN);
            lv_obj_set_style_border_width(c, 1, LV_PART_MAIN);
            lv_obj_set_style_border_color(c, lv_color_hex(0xc7d2fe), LV_PART_MAIN);
            lv_obj_set_style_border_opa(c, LV_OPA_40, LV_PART_MAIN);
            lv_obj_set_style_shadow_width(c, 0, LV_PART_MAIN);
        }
        glassify(c);
    }
}

void decorate(lv_obj_t *scr)
{
    lv_obj_remove_event_cb(scr, starfield_cb);  // a theme change rebuilds the screen
    if (spatial()) {
        lv_obj_add_event_cb(scr, starfield_cb, LV_EVENT_DRAW_MAIN_END, nullptr);
        glassify(scr);
    }
    lv_obj_invalidate(scr);
}

void apply_lvgl_theme(lv_disp_t *disp)
{
    if (spatial()) {  // stock widgets (sliders, switches) pick up the violet accent
        lv_disp_set_theme(disp, lv_theme_default_init(disp, lv_color_hex(SPATIAL_VIOLET), lv_color_hex(SPATIAL_RED),
                                                       true, LV_FONT_DEFAULT));
        return;
    }
    if (ember()) {  // stock widgets (sliders, switches) pick up the orange accent
        lv_disp_set_theme(disp, lv_theme_default_init(disp, lv_color_hex(EMBER_ORANGE), lv_color_hex(EMBER_BAD), true,
                                                       LV_FONT_DEFAULT));
        return;
    }
    if (!paper()) {  // Forge: LVGL's own defaults, re-applied when switching back from another theme
        lv_disp_set_theme(disp, lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE),
                                                      lv_palette_main(LV_PALETTE_RED), LV_THEME_DEFAULT_DARK,
                                                      LV_FONT_DEFAULT));
        return;
    }
    lv_theme_t *t = lv_theme_default_init(disp, lv_color_hex(PAPER_INK), lv_color_hex(PAPER_RED), false,
                                          LV_FONT_DEFAULT);
    lv_disp_set_theme(disp, t);
}

} // namespace theme
