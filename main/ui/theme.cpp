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

namespace theme {

Id current()
{
    static int t = -1;
    if (t < 0) t = settings::get().theme == PAPER ? PAPER : FORGE;
    return (Id) t;
}

bool paper() { return current() == PAPER; }

uint32_t map(uint32_t rgb)
{
    rgb &= 0xffffff;
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
    if (!paper() || !text || !strchr(text, '#')) {
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
    if (paper()) remap_codes(buf);
    lv_label_set_text(label, buf);
}

lv_coord_t shadow(lv_coord_t width) { return paper() ? 0 : width; }

void apply_lvgl_theme(lv_disp_t *disp)
{
    if (!paper()) return;
    lv_theme_t *t = lv_theme_default_init(disp, lv_color_hex(PAPER_INK), lv_color_hex(PAPER_RED), false,
                                          LV_FONT_DEFAULT);
    lv_disp_set_theme(disp, t);
}

} // namespace theme
