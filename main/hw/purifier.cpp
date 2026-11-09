#include "purifier.h"

#if CONFIG_AF_BOARD_BITAIR

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "core/identify.h"
#include "core/state.h"
#include "net/wifi.h"

static const char *TAG = "purifier";

// ---------------------------------------------------------------- mode input
// The purifier's fan-mode line: a PWM whose high time picks the mode (25/65/85 %
// boundaries), or a steady level (low = off, high = turbo). The board pulls it
// up, so an unplugged line reads as turbo.
#define PIN_MODE GPIO_NUM_40
#define SETTLE_US 500000    // a level held this long is a steady level, not PWM
#define STALE_US 250000     // no full cycle for this long: the PWM reading is old
#define MIN_PERIOD_US 8
#define MAX_PERIOD_US 100000
#define MIN_PULSE_US 2
#define NEED_CYCLES 3       // good cycles in a row before the PWM counts
#define MODE_SETTLE_US 200000
#define AVG_SHIFT 4         // 16-cycle running average, against timing jitter under load

struct Capture {
    int64_t last_edge, last_rise, last_cycle;
    uint32_t high_us, pulse_high_us, period_us;  // high_us and period_us are << AVG_SHIFT
    uint8_t good_cycles;
    purifier::Mode mode, pending;
    int64_t pending_since;
    int level;
    bool ready;
};

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static Capture s_cap;

static void IRAM_ATTR on_edge(void *)
{
    int level = gpio_get_level(PIN_MODE);
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL_ISR(&s_lock);
    Capture &c = s_cap;
    if (!c.ready || level == c.level) {
        portEXIT_CRITICAL_ISR(&s_lock);
        return;
    }
    c.level = level;
    c.last_edge = now;
    if (level) {
        int64_t period = now - c.last_rise;
        if (c.last_rise >= 0 && period >= MIN_PERIOD_US && period <= MAX_PERIOD_US && c.pulse_high_us >= MIN_PULSE_US &&
            c.pulse_high_us + MIN_PULSE_US <= period) {
            if (c.good_cycles == 0) {
                c.period_us = (uint32_t) period << AVG_SHIFT;
                c.high_us = c.pulse_high_us << AVG_SHIFT;
            } else {
                c.period_us += (uint32_t) period - (c.period_us >> AVG_SHIFT);
                c.high_us += c.pulse_high_us - (c.high_us >> AVG_SHIFT);
            }
            c.last_cycle = now;
            if (c.good_cycles < NEED_CYCLES) c.good_cycles++;
        } else {
            c.good_cycles = 0;
        }
        c.last_rise = now;
        c.pulse_high_us = 0;
    } else {
        int64_t high = now - c.last_rise;
        if (c.last_rise >= 0 && high >= MIN_PULSE_US && high <= MAX_PERIOD_US) {
            c.pulse_high_us = (uint32_t) high;
        } else {
            c.pulse_high_us = 0;
            c.good_cycles = 0;
        }
    }
    portEXIT_CRITICAL_ISR(&s_lock);
}

static purifier::Mode raw_mode(const Capture &c, int64_t now)
{
    using purifier::Mode;
    if (!c.ready) return Mode::Unknown;
    if (now - c.last_edge >= SETTLE_US) return c.level ? Mode::Turbo : Mode::Off;
    // good PWM that went to a steady high: turbo, without a spell of "unknown" first
    if (c.good_cycles >= NEED_CYCLES && c.level && now - c.last_edge >= STALE_US) return Mode::Turbo;
    if (c.good_cycles < NEED_CYCLES || now - c.last_cycle > STALE_US) return Mode::Unknown;
    uint64_t duty = (uint64_t) c.high_us * 100, p = c.period_us;
    if (duty < p * 25) return Mode::Off;
    if (duty <= p * 65) return Mode::Silent;
    if (duty <= p * 85) return Mode::Auto;
    return Mode::Turbo;
}

// ------------------------------------------------------------------ front LED
#define PIN_LED 41
#define LED_MAX 1023
#define LED_STEP_MS 20

static void led_task(void *)
{
    enum { DARK, BREATHE, FAULT, STEADY, FIND } last = DARK;
    int bright = 5, last_duty = -1;
    bool rising = true;
    int64_t since = 0;
    TickType_t wake = xTaskGetTickCount();
    while (true) {
        int64_t now = esp_timer_get_time() / 1000;
        SysStatus st = g_state.status;
        purifier::Mode m = purifier::mode();
        int show, duty = 0;
        if (identify::active()) show = FIND;
        else if (st == SysStatus::Fault || st == SysStatus::Overheat) show = FAULT;
        else if (!wifi::connected()) show = BREATHE;
        else if (st == SysStatus::Paused || purifier::want_off()) show = DARK;
        else show = STEADY;
        if (show != last) {
            last = (decltype(last)) show;
            bright = 5;
            rising = true;
            since = now;
        }
        switch (show) {
        case FAULT: {  // three short flashes every 2 s, the stock failed-diagnostic pattern
            int64_t ph = (now - since) % 2000;
            duty = ph < 600 && (ph / 100) % 2 == 0 ? LED_MAX : 0;
            break;
        }
        case BREATHE:  // not on Wi-Fi yet: 5 % to 100 % and back, as stock
            if (rising && (bright += 3) >= 100) bright = 100, rising = false;
            else if (!rising && (bright -= 3) < 6) bright = 5, rising = true;
            duty = bright * LED_MAX / 100;
            break;
        case FIND:  // Find this miner, from the web UI
            duty = identify::lit() ? LED_MAX : 0;
            break;
        case STEADY:  // brightness is the purifier's mode, as stock
            duty = m == purifier::Mode::Turbo ? LED_MAX : m == purifier::Mode::Auto ? 0x2ff : 0x1ff;
            break;
        default: duty = 0;
        }
        if (duty != last_duty && ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty) == ESP_OK &&
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0) == ESP_OK)
            last_duty = duty;
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(LED_STEP_MS));
    }
}

static void led_start()
{
    ledc_timer_config_t t = {};
    t.speed_mode = LEDC_LOW_SPEED_MODE;
    t.duty_resolution = LEDC_TIMER_10_BIT;
    t.timer_num = LEDC_TIMER_0;
    t.freq_hz = 5000;
    t.clk_cfg = LEDC_AUTO_CLK;
    ledc_channel_config_t ch = {};
    ch.gpio_num = PIN_LED;
    ch.speed_mode = LEDC_LOW_SPEED_MODE;
    ch.channel = LEDC_CHANNEL_0;
    ch.timer_sel = LEDC_TIMER_0;
    if (ledc_timer_config(&t) != ESP_OK || ledc_channel_config(&ch) != ESP_OK) {
        ESP_LOGW(TAG, "front LED not available");  // mining doesn't need it
        return;
    }
    xTaskCreate(led_task, "led", 2560, NULL, 2, NULL);
}

namespace purifier {

void start()
{
    static bool started = false;
    if (started) return;
    started = true;
    led_start();
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << PIN_MODE;
    io.mode = GPIO_MODE_INPUT;
    io.intr_type = GPIO_INTR_ANYEDGE;
    esp_err_t err = gpio_config(&io);
    if (err == ESP_OK) {
        err = gpio_install_isr_service(0);
        if (err == ESP_ERR_INVALID_STATE) err = ESP_OK;  // someone else installed it
    }
    portENTER_CRITICAL(&s_lock);
    s_cap = {};
    s_cap.last_edge = esp_timer_get_time();
    s_cap.last_rise = -1;
    s_cap.level = gpio_get_level(PIN_MODE);
    portEXIT_CRITICAL(&s_lock);
    if (err == ESP_OK) err = gpio_isr_handler_add(PIN_MODE, on_edge, NULL);
    if (err != ESP_OK) {
        // without the input the mode stays unknown, and unknown keeps the chips off
        ESP_LOGE(TAG, "fan-mode input on GPIO%d failed: %s", PIN_MODE, esp_err_to_name(err));
        return;
    }
    portENTER_CRITICAL(&s_lock);
    s_cap.ready = true;
    portEXIT_CRITICAL(&s_lock);
    // a valid reading takes three PWM cycles, or half a second of a steady level
    for (int i = 0; i < 15 && mode() == Mode::Unknown; i++) vTaskDelay(pdMS_TO_TICKS(50));
    ESP_LOGI(TAG, "purifier mode: %s", mode_name());
}

Mode mode()
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_lock);
    Capture &c = s_cap;
    Mode m = raw_mode(c, now);
    // a new mode has to hold for MODE_SETTLE_US; losing the signal counts at once
    if (m == Mode::Unknown || c.mode == Mode::Unknown || m == c.mode) {
        c.mode = c.pending = m;
        c.pending_since = now;
    } else if (c.pending != m) {
        c.pending = m;
        c.pending_since = now;
    } else if (now - c.pending_since >= MODE_SETTLE_US) {
        c.mode = m;
    }
    m = c.mode;
    portEXIT_CRITICAL(&s_lock);
    return m;
}

const char *mode_name()
{
    switch (mode()) {
    case Mode::Off: return "off";
    case Mode::Silent: return "silent";
    case Mode::Auto: return "auto";
    case Mode::Turbo: return "turbo";
    default: return "unknown";
    }
}

bool want_off()
{
    Mode m = mode();
    return m == Mode::Off || m == Mode::Unknown;
}

// The stock fan driver scales its output by 0.75, so its 50/75/100 % presets come
// out as these duties (skot's reverse-engineering notes).
float fan_floor_pct()
{
    switch (mode()) {
    case Mode::Silent: return 37.5f;
    case Mode::Auto: return 56.25f;
    case Mode::Turbo: return 75.0f;
    case Mode::Off: return 0;
    default: return 100;  // can't read the panel: full air
    }
}

} // namespace purifier

#endif
