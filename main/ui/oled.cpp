// 128x32 SSD1306 OLED on the Bitaxe I2C bus. Replaces the LVGL screen UI on
// boards without an LCD: a 512-byte framebuffer, text drawn straight from the
// LVGL font tables, refreshed once a second. BOOT button: short press = next
// page, long press = screen off/on.
#include "sdkconfig.h"
#if CONFIG_AF_BOARD_SUPRAHEX

#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "core/board.h"
#include "core/settings.h"
#include "core/state.h"
#include "hw/i2c_bus.h"
#include "ui/display.h"
#include "ui/ui.h"

static const char *TAG = "oled";

#define OLED_ADDR 0x3C
#define W 128
#define H 32
#define PIN_BOOT GPIO_NUM_0
#define LONG_PRESS_MS 1500

enum Page { PAGE_MINER, PAGE_POWER, PAGE_POOL, PAGE_NET, PAGE_COUNT };

static uint8_t s_fb[W * H / 8];
static volatile int s_page = 0;
static volatile int s_page_delta = 0;
static bool s_on = true;
static bool s_ok = false;
static bool s_flip = false;

// ------------------------------------------------------------------ panel
static bool cmd(const uint8_t *c, size_t n)
{
    uint8_t buf[16];
    buf[0] = 0x00;  // control byte: command stream
    memcpy(buf + 1, c, n);
    return i2c_bus::write(OLED_ADDR, buf, n + 1) == ESP_OK;
}

static bool panel_init()
{
    static const uint8_t seq[] = {
        0xAE,        // display off
        0xD5, 0x80,  // clock divide
        0xA8, 0x1F,  // multiplex: 32 rows
        0xD3, 0x00,  // display offset
        0x40,        // start line 0
        0x8D, 0x14,  // charge pump on
        0x20, 0x00,  // horizontal addressing
        0xDA, 0x02,  // COM pins for 128x32
        0xD9, 0xF1,  // pre-charge
        0xDB, 0x40,  // VCOMH
        0xA4,        // follow RAM
        0xA6,        // normal (not inverted)
    };
    bool ok = true;
    for (size_t i = 0; i < sizeof(seq);) {
        size_t n = (seq[i] == 0xD5 || seq[i] == 0xA8 || seq[i] == 0xD3 || seq[i] == 0x8D || seq[i] == 0x20 ||
                    seq[i] == 0xDA || seq[i] == 0xD9 || seq[i] == 0xDB) ? 2 : 1;
        ok &= cmd(seq + i, n);
        i += n;
    }
    return ok;
}

static void panel_orientation(bool flip)
{
    uint8_t c[2] = {(uint8_t) (flip ? 0xA0 : 0xA1), (uint8_t) (flip ? 0xC0 : 0xC8)};
    cmd(c, 2);
}

static void panel_contrast(uint8_t pct)
{
    uint8_t c[2] = {0x81, (uint8_t) (pct * 255 / 100)};
    cmd(c, 2);
}

static void panel_power(bool on)
{
    uint8_t c = on ? 0xAF : 0xAE;
    cmd(&c, 1);
}

static void flush()
{
    static const uint8_t win[] = {0x21, 0, W - 1, 0x22, 0, H / 8 - 1};
    cmd(win, sizeof(win));
    uint8_t buf[1 + W];
    buf[0] = 0x40;  // control byte: data
    for (int page = 0; page < H / 8; page++) {
        memcpy(buf + 1, s_fb + page * W, W);
        i2c_bus::write(OLED_ADDR, buf, sizeof(buf));
    }
}

// ------------------------------------------------------------------ drawing
static void px(int x, int y)
{
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    s_fb[(y / 8) * W + x] |= 1 << (y & 7);
}

static int glyph(const lv_font_t *f, int x, int top, uint32_t ch)
{
    lv_font_glyph_dsc_t g;
    if (!lv_font_get_glyph_dsc(f, &g, ch, 0)) return 0;
    const uint8_t *bmp = lv_font_get_glyph_bitmap(f, ch);
    if (bmp && g.box_w && g.box_h) {
        const int bpp = g.bpp, mask = (1 << bpp) - 1, half = (mask + 1) / 2;
        const int baseline = top + f->line_height - f->base_line;
        const int y0 = baseline - g.box_h - g.ofs_y;
        for (int y = 0; y < g.box_h; y++) {
            for (int x0 = 0; x0 < g.box_w; x0++) {
                int bit = (y * g.box_w + x0) * bpp;
                int v = (bmp[bit >> 3] >> (8 - bpp - (bit & 7))) & mask;
                if (v >= half) px(x + g.ofs_x + x0, y0 + y);
            }
        }
    }
    return g.adv_w;
}

static int text_w(const lv_font_t *f, const char *s)
{
    int w = 0;
    lv_font_glyph_dsc_t g;
    for (; *s; s++)
        if (lv_font_get_glyph_dsc(f, &g, (uint8_t) *s, 0)) w += g.adv_w;
    return w;
}

// align: 0 left, 1 centre, 2 right
static void text(const lv_font_t *f, int x, int top, const char *s, int align = 0)
{
    if (align == 1) x = (W - text_w(f, s)) / 2;
    else if (align == 2) x = W - text_w(f, s);
    for (; *s; s++) x += glyph(f, x, top, (uint8_t) *s);
}

static void fmt_hashrate(char *out, size_t n, float ghs)
{
    if (ghs >= 1000) snprintf(out, n, "%.2f TH/s", ghs / 1000);
    else snprintf(out, n, "%.0f GH/s", ghs);
}

static void fmt_diff(char *out, size_t n, double d)
{
    const char *u = "";
    if (d >= 1e12) d /= 1e12, u = "T";
    else if (d >= 1e9) d /= 1e9, u = "G";
    else if (d >= 1e6) d /= 1e6, u = "M";
    else if (d >= 1e3) d /= 1e3, u = "K";
    snprintf(out, n, "%.2f%s", d, u);
}

static void render()
{
    const MinerState &s = g_state;
    const lv_font_t *big = &lv_font_montserrat_20;
    const lv_font_t *sm = &lv_font_unscii_8;
    char a[40], b[40];
    memset(s_fb, 0, sizeof(s_fb));

    if (s.status == SysStatus::Fault || s.status == SysStatus::Overheat) {
        text(sm, 0, 0, s.status == SysStatus::Overheat ? "OVERHEAT" : "FAULT", 1);
        // wrap the reason over up to three lines of 16 characters
        size_t len = strlen(s.fault);
        for (int line = 0; line < 3 && line * 16 < (int) len; line++) {
            snprintf(a, 17, "%s", s.fault + line * 16);
            text(sm, 0, 8 + line * 8, a);
        }
        return;
    }
    if (s.status == SysStatus::SetupAP) {
        text(sm, 0, 0, "Wi-Fi setup", 1);
        text(sm, 0, 12, s.ap_ssid, 1);
        text(sm, 0, 24, "192.168.4.1", 1);
        return;
    }

    switch (s_page) {
    case PAGE_MINER:
        fmt_hashrate(a, sizeof(a), s.hashrate_ghs);
        text(big, 0, 0, a, 1);
        snprintf(b, sizeof(b), "%.0fC %.0fW %uM", s.chip_temp_max, s.pin, s.freq_mhz);
        text(sm, 0, 24, b, 1);
        break;
    case PAGE_POWER:
        snprintf(a, sizeof(a), "ASIC %.0fC VR %.0fC", s.chip_temp_max, s.vr_temp);
        text(sm, 0, 0, a);
        snprintf(a, sizeof(a), "%.0fW  %.2fV in", s.pin, s.vin);
        text(sm, 0, 8, a);
        snprintf(a, sizeof(a), "%umV %uMHz", s.core_mv, s.freq_mhz);
        text(sm, 0, 16, a);
        snprintf(a, sizeof(a), "Fan %u%% %urpm", s.fan_pct, s.fan_rpm[0]);
        text(sm, 0, 24, a);
        break;
    case PAGE_POOL:
        snprintf(a, sizeof(a), "%.16s", s.pool_connected ? s.pool_host : "pool: offline");
        text(sm, 0, 0, a);
        snprintf(a, sizeof(a), "A %lu  R %lu", (unsigned long) s.shares_accepted, (unsigned long) s.shares_rejected);
        text(sm, 0, 8, a);
        fmt_diff(b, sizeof(b), s.best_diff);
        snprintf(a, sizeof(a), "Best %s", b);
        text(sm, 0, 16, a);
        fmt_diff(b, sizeof(b), s.session_best);
        snprintf(a, sizeof(a), "Sess %s", b);
        text(sm, 0, 24, a);
        break;
    case PAGE_NET: {
        text(sm, 0, 0, "AxeForge", 1);
        text(sm, 0, 8, s.ip[0] ? s.ip : "no network", 1);
        snprintf(a, sizeof(a), "%.16s", s.ssid);
        text(sm, 0, 16, a, 1);
        uint32_t up = s.uptime_s;
        snprintf(a, sizeof(a), "up %lud %02lu:%02lu", (unsigned long) (up / 86400), (unsigned long) (up / 3600 % 24),
                 (unsigned long) (up / 60 % 60));
        text(sm, 0, 24, a, 1);
        break;
    }
    }
}

// ------------------------------------------------------------------ task
static void oled_task(void *)
{
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << PIN_BOOT;
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io);

    // the I2C bus is brought up by board::init_hardware()
    for (int i = 0; i < 50 && !s_ok; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        s_ok = i2c_bus::probe(OLED_ADDR) && panel_init();
    }
    if (!s_ok) {
        ESP_LOGW(TAG, "no SSD1306 at 0x3C, running headless");
        vTaskDelete(NULL);
    }
    s_flip = settings::get().rotate_180;
    panel_orientation(s_flip);
    panel_contrast(settings::get().brightness);
    panel_power(true);

    int held_ms = 0, since_draw = 1000, since_rotate = 0;
    bool long_done = false;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(50));
        since_draw += 50;
        bool pressed = gpio_get_level(PIN_BOOT) == 0;
        if (pressed) {
            held_ms += 50;
            if (held_ms >= LONG_PRESS_MS && !long_done) {
                long_done = true;
                s_on = !s_on;
                panel_power(s_on);
            }
        } else if (held_ms) {
            if (!long_done) s_page_delta++;
            held_ms = 0;
            long_done = false;
        }
        int rot = settings::get().page_rotate_s;
        if (rot && (since_rotate += 50) >= rot * 1000) {
            since_rotate = 0;
            s_page_delta++;
        }
        if (s_page_delta) {
            s_page = ((s_page + s_page_delta) % PAGE_COUNT + PAGE_COUNT) % PAGE_COUNT;
            s_page_delta = 0;
            since_draw = 1000;
        }
        if (since_draw >= 1000 && s_on) {
            since_draw = 0;
            render();
            flush();
        }
    }
}

// ------------------------------------------------------------------ API used by web/main
namespace display {
void start() { xTaskCreatePinnedToCore(oled_task, "oled", 4096, NULL, 2, NULL, 0); }
DisplayType type() { return DisplayType::Big; }
bool small() { return false; }
int width() { return W; }
int height() { return H; }
void set_brightness(uint8_t pct)
{
    if (s_ok) panel_contrast(pct);
}
void set_rotation(bool rotate_180)
{
    s_flip = rotate_180;
    if (s_ok) panel_orientation(rotate_180);
}
lv_img_dsc_t *snapshot(int) { return nullptr; }  // no LCD to capture
void free_snapshot(lv_img_dsc_t *) {}
}

namespace ui {
void build() {}
void on_button(int) { s_page_delta++; }
void on_long_press(int) {}
void request_page(int delta) { s_page_delta += delta; }
void request_kind(char) {}
void reload_pages() {}
int current_page() { return s_page; }
char current_kind() { return 'o'; }
}

#endif // CONFIG_AF_BOARD_SUPRAHEX
