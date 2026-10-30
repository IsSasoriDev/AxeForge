#include "display.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#define THEME_IMPL
#include "ui/theme.h"
#undef THEME_IMPL
#include "sdkconfig.h"

#if CONFIG_AF_LCD_ST7796
#include "esp_lcd_st7796.h"
#else
#include "esp_lcd_ili9488.h"
#endif

#include "core/settings.h"
#include "core/state.h"
#include "ui/ui.h"
#include "ui/ui_small.h"

static const char *TAG = "display";

#ifndef CONFIG_AF_LCD_ROTATE_180
#define CONFIG_AF_LCD_ROTATE_180 0
#endif

#define LCD_HOST SPI2_HOST
#define BUF_LINES 20
#ifdef AF_PIN_BTN_1
#define PIN_BTN_1 ((gpio_num_t) AF_PIN_BTN_1)
#else
#define PIN_BTN_1 GPIO_NUM_14
#endif
#define PIN_BTN_2 GPIO_NUM_0
#define LONG_PRESS_MS 1500
#define TDISPLAY_PCLK_HZ 6528000  // same as the stock firmware
#define TDISPLAY_GAP_Y 35         // the 170-line panel sits 35 lines into the ST7789's 240

static esp_lcd_panel_handle_t s_panel;
static DisplayType s_type = DisplayType::Big;
static int s_w = LCD_H_RES, s_h = LCD_V_RES;
static lv_disp_drv_t s_disp_drv;
static lv_disp_draw_buf_t s_draw_buf;
static bool s_bl_pwm = false;

// Screen-off timer: the backlight goes dark after settings.screen_off_min without
// a button press or a settings change. A press while dark only wakes the screen;
// a fault, overheating or a found block lights it again by itself.
static int64_t s_last_input_ms = 0;
static bool s_asleep = false;
static uint32_t s_blocks_seen = 0;

static void backlight(uint8_t pct)
{
    if (!s_bl_pwm) return;
    if (pct > 100) pct = 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 1023 * pct / 100);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
}

static void screen_wake()
{
    s_asleep = false;
    backlight(settings::get().brightness);
}

static void screen_idle_check()
{
    int64_t now = esp_timer_get_time() / 1000;
    bool alarm = g_state.status == SysStatus::Fault || g_state.status == SysStatus::Overheat ||
                 g_state.blocks_found != s_blocks_seen;
    if (alarm) {
        s_last_input_ms = now;
        s_blocks_seen = g_state.blocks_found;
        if (s_asleep) screen_wake();
        return;
    }
    uint8_t min = settings::get().screen_off_min;
    if (!s_asleep && min && now - s_last_input_ms > (int64_t) min * 60000) {
        s_asleep = true;
        backlight(0);
        ESP_LOGI(TAG, "screen off after %u min idle", min);
    }
}
static volatile int s_pending_rot = -1;
static volatile bool s_snap_req = false;
static volatile bool s_rebuild_req = false;  // rebuild the screen UI (theme change)
static lv_img_dsc_t *s_snap = nullptr;
static SemaphoreHandle_t s_snap_done;  // set from other tasks, applied by the display task

static bool on_trans_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *ctx)
{
    lv_disp_flush_ready((lv_disp_drv_t *) ctx);
    return false;
}

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *px)
{
#if CONFIG_AF_LCD_BUS_SPI && CONFIG_AF_LCD_ILI9488
    if (s_type == DisplayType::Big) {
    // the ILI9488 driver converts native-endian RGB565 to RGB666, but LVGL
    // renders byte-swapped colors for SPI, so undo that here
    uint16_t *p = (uint16_t *) px;
    size_t n = (size_t) (a->x2 - a->x1 + 1) * (a->y2 - a->y1 + 1);
    for (size_t i = 0; i < n; i++) p[i] = __builtin_bswap16(p[i]);
    }
#endif
    esp_lcd_panel_draw_bitmap(s_panel, a->x1, a->y1, a->x2 + 1, a->y2 + 1, px);
}

static void tick_cb(void *) { lv_tick_inc(2); }

static void backlight_init()
{
    if (CONFIG_AF_LCD_PIN_BL < 0) return;
    ledc_timer_config_t t = {};
    t.speed_mode = LEDC_LOW_SPEED_MODE;
    t.duty_resolution = LEDC_TIMER_10_BIT;
    t.timer_num = LEDC_TIMER_1;
    t.freq_hz = 5000;
    t.clk_cfg = LEDC_AUTO_CLK;
    ledc_timer_config(&t);
    ledc_channel_config_t c = {};
    c.gpio_num = CONFIG_AF_LCD_PIN_BL;
    c.speed_mode = LEDC_LOW_SPEED_MODE;
    c.channel = LEDC_CHANNEL_1;
    c.timer_sel = LEDC_TIMER_1;
    c.duty = 0;
    ledc_channel_config(&c);
    s_bl_pwm = true;
}

#if AF_LCD_SPI_ST7789
// HX-6: Waveshare 2.8in module (ST7789, 240x320 portrait) over SPI, run in
// landscape. The 320x170 screen layout sits at the top of its 320x240.
static void panel_init()
{
    backlight_init();
    spi_bus_config_t bus = {};
    bus.sclk_io_num = 12;
    bus.mosi_io_num = 11;
    bus.miso_io_num = -1;  // MISO (GPIO2) is wired but never needed
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = s_w * BUF_LINES * sizeof(uint16_t);
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t io_cfg = {};
    io_cfg.dc_gpio_num = 6;
    io_cfg.cs_gpio_num = 7;
    io_cfg.pclk_hz = 40 * 1000 * 1000;
    io_cfg.lcd_cmd_bits = 8;
    io_cfg.lcd_param_bits = 8;
    io_cfg.spi_mode = 0;
    io_cfg.trans_queue_depth = 10;
    io_cfg.on_color_trans_done = on_trans_done;
    io_cfg.user_ctx = &s_disp_drv;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t) LCD_HOST, &io_cfg, &io));

    esp_lcd_panel_dev_config_t pc = {};
    pc.reset_gpio_num = 5;
    pc.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    pc.bits_per_pixel = 16;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &pc, &s_panel));
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_invert_color(s_panel, true);  // IPS panel
    esp_lcd_panel_swap_xy(s_panel, true);
    bool flip = settings::get().rotate_180;
    esp_lcd_panel_mirror(s_panel, !flip, flip);
    esp_lcd_panel_disp_on_off(s_panel, true);
    ESP_LOGI(TAG, "2.8in SPI panel ready (%dx%d)", s_w, s_h);
}
#else
static void panel_init()
{
    if (CONFIG_AF_LCD_PIN_PWR >= 0) {
        gpio_reset_pin((gpio_num_t) CONFIG_AF_LCD_PIN_PWR);
        gpio_set_direction((gpio_num_t) CONFIG_AF_LCD_PIN_PWR, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t) CONFIG_AF_LCD_PIN_PWR, 1);
    }
    backlight_init();

    esp_lcd_panel_io_handle_t io;
#if CONFIG_AF_LCD_BUS_I80
    if (CONFIG_AF_LCD_PIN_RD >= 0) {
        // the panel's read strobe must idle high or it fights the data bus
        gpio_reset_pin((gpio_num_t) CONFIG_AF_LCD_PIN_RD);
        gpio_set_direction((gpio_num_t) CONFIG_AF_LCD_PIN_RD, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t) CONFIG_AF_LCD_PIN_RD, 1);
    }
    esp_lcd_i80_bus_handle_t i80;
    esp_lcd_i80_bus_config_t bus = {};
    bus.clk_src = LCD_CLK_SRC_DEFAULT;
    bus.dc_gpio_num = CONFIG_AF_LCD_PIN_DC;
    bus.wr_gpio_num = CONFIG_AF_LCD_PIN_WR;
    const int data_pins[8] = {CONFIG_AF_LCD_PIN_D0, CONFIG_AF_LCD_PIN_D1, CONFIG_AF_LCD_PIN_D2, CONFIG_AF_LCD_PIN_D3,
                              CONFIG_AF_LCD_PIN_D4, CONFIG_AF_LCD_PIN_D5, CONFIG_AF_LCD_PIN_D6, CONFIG_AF_LCD_PIN_D7};
    for (int i = 0; i < 8; i++) bus.data_gpio_nums[i] = data_pins[i];
    bus.bus_width = 8;
    bus.max_transfer_bytes = s_w * BUF_LINES * sizeof(uint16_t);
    ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&bus, &i80));

    esp_lcd_panel_io_i80_config_t io_cfg = {};
    io_cfg.cs_gpio_num = CONFIG_AF_LCD_PIN_CS;
    io_cfg.pclk_hz = s_type == DisplayType::TDisplay ? TDISPLAY_PCLK_HZ : CONFIG_AF_LCD_I80_KHZ * 1000;
    io_cfg.trans_queue_depth = 10;
    io_cfg.on_color_trans_done = on_trans_done;
    io_cfg.user_ctx = &s_disp_drv;
    io_cfg.lcd_cmd_bits = 8;
    io_cfg.lcd_param_bits = 8;
    io_cfg.dc_levels.dc_idle_level = 0;
    io_cfg.dc_levels.dc_cmd_level = 0;
    io_cfg.dc_levels.dc_dummy_level = 0;
    io_cfg.dc_levels.dc_data_level = 1;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(i80, &io_cfg, &io));
#else
    spi_bus_config_t bus = {};
    bus.sclk_io_num = CONFIG_AF_LCD_PIN_SCLK;
    bus.mosi_io_num = CONFIG_AF_LCD_PIN_MOSI;
    bus.miso_io_num = -1;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = LCD_H_RES * BUF_LINES * 3;
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {};
    io_cfg.dc_gpio_num = CONFIG_AF_LCD_PIN_DC;
    io_cfg.cs_gpio_num = CONFIG_AF_LCD_PIN_CS;
    io_cfg.pclk_hz = CONFIG_AF_LCD_SPI_MHZ * 1000 * 1000;
    io_cfg.lcd_cmd_bits = 8;
    io_cfg.lcd_param_bits = 8;
    io_cfg.spi_mode = 0;
    io_cfg.trans_queue_depth = 10;
    io_cfg.on_color_trans_done = on_trans_done;
    io_cfg.user_ctx = &s_disp_drv;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t) LCD_HOST, &io_cfg, &io));
#endif

    esp_lcd_panel_dev_config_t pc = {};
    pc.reset_gpio_num = CONFIG_AF_LCD_PIN_RST;
#if CONFIG_AF_LCD_BUS_I80
    if (s_type == DisplayType::TDisplay) {
        // LilyGo T-Display-S3 (the NerdQaxe++ stock screen): ST7789, 170x320
        pc.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        pc.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &pc, &s_panel));
        esp_lcd_panel_reset(s_panel);
        esp_lcd_panel_init(s_panel);
        esp_lcd_panel_invert_color(s_panel, true);
        esp_lcd_panel_swap_xy(s_panel, true);
        bool flip = settings::get().rotate_180;
        esp_lcd_panel_mirror(s_panel, !flip, flip);
        esp_lcd_panel_set_gap(s_panel, 0, TDISPLAY_GAP_Y);
        esp_lcd_panel_disp_on_off(s_panel, true);
        ESP_LOGI(TAG, "T-Display-S3 panel ready (%dx%d)", s_w, s_h);
        return;
    }
#endif
#if CONFIG_AF_LCD_BGR
    pc.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
#else
    pc.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
#endif
#if CONFIG_AF_LCD_ST7796
    pc.bits_per_pixel = 16;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7796(io, &pc, &s_panel));
#elif CONFIG_AF_LCD_BUS_I80
    pc.bits_per_pixel = 16;  // parallel ILI9488 takes RGB565 directly
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9488(io, &pc, 0, &s_panel));
#else
    pc.bits_per_pixel = 18;  // SPI ILI9488 only accepts RGB666
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9488(io, &pc, s_w * BUF_LINES * 3, &s_panel));
#endif
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_invert_color(s_panel, CONFIG_AF_LCD_INVERT_COLORS);
    // native panel is 320x480 portrait; swap for landscape
    esp_lcd_panel_swap_xy(s_panel, true);
    bool flip = settings::get().rotate_180 != (bool) CONFIG_AF_LCD_ROTATE_180;
    esp_lcd_panel_mirror(s_panel, flip, flip);
    esp_lcd_panel_disp_on_off(s_panel, true);
    ESP_LOGI(TAG, "3.5in panel ready (%dx%d)", s_w, s_h);
}
#endif  // AF_LCD_SPI_ST7789

static void lvgl_init()
{
    lv_init();
    size_t bytes = s_w * BUF_LINES * sizeof(lv_color_t);
    void *b1 = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void *b2 = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    lv_disp_draw_buf_init(&s_draw_buf, b1, b2, s_w * BUF_LINES);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = s_w;
    s_disp_drv.ver_res = s_h;
    s_disp_drv.flush_cb = flush_cb;
    s_disp_drv.draw_buf = &s_draw_buf;
    theme::apply_lvgl_theme(lv_disp_drv_register(&s_disp_drv));

    esp_timer_create_args_t ta = {};
    ta.callback = tick_cb;
    ta.name = "lv_tick";
    esp_timer_handle_t t;
    esp_timer_create(&ta, &t);
    esp_timer_start_periodic(t, 2000);
}

static void buttons_init()
{
    gpio_config_t c = {};
    c.pin_bit_mask = (1ULL << PIN_BTN_1) | (1ULL << PIN_BTN_2);
    c.mode = GPIO_MODE_INPUT;
    c.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&c);
}

// Polled from the display task: short press = next/prev page,
// long press on button 1 = toggle backlight. A press on a dark screen only wakes it.
static void buttons_poll()
{
    static int64_t down_at[2] = {0, 0};
    static bool long_fired[2] = {false, false};
    static bool swallow[2] = {false, false};  // the press that woke the screen
    const gpio_num_t pins[2] = {PIN_BTN_1, PIN_BTN_2};
    int64_t now = esp_timer_get_time() / 1000;
    for (int i = 0; i < 2; i++) {
        bool down = gpio_get_level(pins[i]) == 0;
        if (down && !down_at[i]) {
            down_at[i] = now;
            long_fired[i] = false;
            s_last_input_ms = now;
            swallow[i] = s_asleep;
            if (s_asleep) screen_wake();
        } else if (down && !long_fired[i] && !swallow[i] && now - down_at[i] > LONG_PRESS_MS) {
            long_fired[i] = true;
            if (s_type == DisplayType::TDisplay) ui_small::on_long_press(i);
            else ui::on_long_press(i);
        } else if (!down && down_at[i]) {
            if (!long_fired[i] && !swallow[i] && now - down_at[i] > 30) {
                if (s_type == DisplayType::TDisplay) ui_small::on_button(i);
                else ui::on_button(i);
            }
            down_at[i] = 0;
        }
    }
}

// Holding the bottom button at power-on swaps the screen type, so a wrong
// choice can be fixed without the web UI.
static void boot_override()
{
    if (gpio_get_level(PIN_BTN_1) != 0) return;
    vTaskDelay(pdMS_TO_TICKS(800));
    if (gpio_get_level(PIN_BTN_1) != 0) return;
    Settings &cfg = settings::get();
    cfg.display_type = cfg.display_type ? 0 : 1;
    settings::save();
    ESP_LOGW(TAG, "button held at boot: display type -> %s", cfg.display_type ? "T-Display-S3" : "3.5in");
    while (gpio_get_level(PIN_BTN_1) == 0) vTaskDelay(pdMS_TO_TICKS(50));  // don't count it as a press
}

static void display_task(void *)
{
    buttons_init();
    boot_override();
#if AF_LCD_SPI_ST7789
    s_type = DisplayType::TDisplay;  // the small layout, on a 320x240 panel
    s_w = 320;
    s_h = 240;
#else
#if CONFIG_AF_LCD_BUS_I80
    s_type = settings::get().display_type == 1 ? DisplayType::TDisplay : DisplayType::Big;
#else
    s_type = DisplayType::Big;  // the T-Display is wired to the parallel bus only
#endif
    if (s_type == DisplayType::TDisplay) {
        s_w = 320;
        s_h = 170;
    }
#endif
    panel_init();
    lvgl_init();
    if (s_type == DisplayType::TDisplay) ui_small::build();
    else ui::build();
    display::set_brightness(settings::get().brightness);
    while (true) {
        if (s_pending_rot >= 0) {
            bool rot = s_pending_rot != 0;
            s_pending_rot = -1;
            if (s_type == DisplayType::TDisplay) {
                esp_lcd_panel_mirror(s_panel, !rot, rot);
            } else {
                bool flip = rot != (bool) CONFIG_AF_LCD_ROTATE_180;
                esp_lcd_panel_mirror(s_panel, flip, flip);
            }
            lv_obj_invalidate(lv_scr_act());
        }
        if (s_rebuild_req) {
            // theme change: every colour was mapped when the widgets were built,
            // so drop them all and build again under the new palette
            s_rebuild_req = false;
            lv_obj_clean(lv_scr_act());
            theme::apply_lvgl_theme(lv_disp_get_default());
            if (s_type == DisplayType::TDisplay) ui_small::build();
            else ui::build();
        }
        if (s_snap_req) {
            s_snap = lv_snapshot_take(lv_scr_act(), LV_IMG_CF_TRUE_COLOR);
            s_snap_req = false;
            xSemaphoreGive(s_snap_done);
        }
        buttons_poll();
        screen_idle_check();
        uint32_t wait = lv_timer_handler();
        if (wait > 20) wait = 20;
        if (wait < 5) wait = 5;
        vTaskDelay(pdMS_TO_TICKS(wait));
    }
}

namespace display {

void start()
{
    s_snap_done = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(display_task, "display", 8192, NULL, 3, NULL, 0);
}

DisplayType type() { return s_type; }
bool small() { return s_type == DisplayType::TDisplay; }
int width() { return s_w; }
int height() { return s_h; }

lv_img_dsc_t *snapshot(int timeout_ms)
{
    xSemaphoreTake(s_snap_done, 0);
    s_snap = nullptr;
    s_snap_req = true;
    if (xSemaphoreTake(s_snap_done, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        s_snap_req = false;
        return nullptr;
    }
    return s_snap;
}

void free_snapshot(lv_img_dsc_t *img)
{
    if (img) lv_snapshot_free(img);
}

void set_rotation(bool rotate_180) { s_pending_rot = rotate_180 ? 1 : 0; }
void rebuild_ui() { s_rebuild_req = true; }

// From outside (boot, a settings save, the long-press toggle): counts as activity,
// and 0 means asleep so the next press wakes the screen.
void set_brightness(uint8_t pct)
{
    s_last_input_ms = esp_timer_get_time() / 1000;
    s_asleep = pct == 0;
    backlight(pct);
}

} // namespace display
