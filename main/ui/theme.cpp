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

Id current()
{
    static int t = -1;
    if (t < 0) {
        uint8_t s = settings::get().theme;
        t = s == PAPER ? PAPER : s == EMBER ? EMBER : FORGE;
    }
    return (Id) t;
}

bool paper() { return current() == PAPER; }
bool ember() { return current() == EMBER; }

uint32_t map(uint32_t rgb)
{
    rgb &= 0xffffff;
    if (ember()) {
        for (const Pair &p : EMBER_MAP)
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

void apply_lvgl_theme(lv_disp_t *disp)
{
    if (ember()) {  // stock widgets (sliders, switches) pick up the orange accent
        lv_disp_set_theme(disp, lv_theme_default_init(disp, lv_color_hex(EMBER_ORANGE), lv_color_hex(EMBER_BAD), true,
                                                       LV_FONT_DEFAULT));
        return;
    }
    if (!paper()) return;
    lv_theme_t *t = lv_theme_default_init(disp, lv_color_hex(PAPER_INK), lv_color_hex(PAPER_RED), false,
                                          LV_FONT_DEFAULT);
    lv_disp_set_theme(disp, t);
}

} // namespace theme
