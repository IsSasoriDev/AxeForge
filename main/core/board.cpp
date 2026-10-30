// The NerdQaxe family: NerdQaxe++ (rev5.1/rev6: TPS53647, rev7: TPS546 stack),
// NerdQAxe+, NerdOctaxe+, NerdOctaxe-g and NerdQX. They share the ESP32 module,
// regulator wiring, fans and board sensors; board_config.h holds what differs.
// The NerdAxe and the Supra Hex have their own files; exactly one is compiled.
#include "core/board_config.h"
#if AF_NQ_FAMILY

#include "board.h"

#include <math.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "asic/bm13xx.h"
#include "core/settings.h"
#include "core/state.h"
#include "hw/i2c_bus.h"
#include "hw/sensors.h"
#include "hw/tmp451.h"
#include "hw/tps53647.h"
#include "hw/tps546.h"

static const char *TAG = "board";

#define PIN_VR_EN GPIO_NUM_10
#define PIN_LDO_EN GPIO_NUM_13
#define PIN_ASIC_RST GPIO_NUM_1

#define PIN_VR_DETECT GPIO_NUM_3  // NerdOctaxe-g rev3.0+: strapped high for a TPS53667
#define G67_4PH_FREQ 600          // settings.cpp's NerdOctaxe-g defaults
#define G67_4PH_MV 1150

#define START_DIFFICULTY AF_ASIC_DIFF_MAX
#if AF_ASIC_BM1368
#define NO_CHIPS_TEXT "No BM1368 chips answered"
#else
#define NO_CHIPS_TEXT "No BM1370 chips answered"
#endif

// The TMP1075s sit on the PCB next to the chips; the die runs ~10 C hotter.
// The NerdQaxe++ adds this on rev7 only; rev <= 6 reports the raw sensor (PR #6).
#define REV7_DIE_TEMP_OFFSET 10.0f

static bool s_powered = false;
static bool s_rev7 = false;
static int s_phases = AF_VR_PHASES, s_imax = AF_VR_IMAX_A;
static float s_ifault = AF_VR_IFAULT_A;
[[maybe_unused]] static float s_vr_offset = 0;  // the TPS53667 board's VR sensor reads low
static uint16_t s_max_mv = CONFIG_AF_MAX_CORE_MV;
static int s_die_sensors = 0;  // chips with a TMP451 die reading
static board::Limits s_limits = {CONFIG_AF_MAX_FREQ_MHZ, CONFIG_AF_MAX_POWER_W, CONFIG_AF_MAX_VR_TEMP,
                                 CONFIG_AF_CRIT_VR_TEMP};
#if AF_DETECT_REV7
static const board::Limits REV7_LIMITS = {CONFIG_AF_REV7_MAX_FREQ_MHZ, CONFIG_AF_REV7_MAX_POWER_W,
                                          CONFIG_AF_REV7_MAX_VR_TEMP, CONFIG_AF_REV7_CRIT_VR_TEMP};
#endif
static SemaphoreHandle_t s_lock;  // monitor and tuner both drive power

namespace {
struct Guard {
    Guard() { xSemaphoreTakeRecursive(s_lock, portMAX_DELAY); }
    ~Guard() { xSemaphoreGiveRecursive(s_lock); }
};
}
static uint16_t s_mv = 0;
static uint16_t s_freq = 0;

static void out_pin(gpio_num_t pin, int level)
{
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, level);
}

namespace board {

const Limits &limits() { return s_limits; }
bool is_rev7() { return s_rev7; }

uint16_t clamp_mv(int mv)
{
    if (mv < CONFIG_AF_MIN_CORE_MV) mv = CONFIG_AF_MIN_CORE_MV;
    if (mv > s_max_mv) mv = s_max_mv;
    return (uint16_t) mv;
}

uint16_t max_mv() { return s_max_mv; }

uint16_t clamp_freq(int mhz)
{
    if (mhz < AF_MIN_FREQ_MHZ) mhz = AF_MIN_FREQ_MHZ;
    if (mhz > s_limits.max_freq_mhz) mhz = s_limits.max_freq_mhz;
    return (uint16_t) mhz;
}

Error init_hardware()
{
    s_lock = xSemaphoreCreateRecursiveMutex();
    out_pin(PIN_VR_EN, 0);
    out_pin(PIN_LDO_EN, 0);
    out_pin(PIN_ASIC_RST, 0);

    if (i2c_bus::init() != ESP_OK) return Error::NoRegulator;
    fan::init();
    fan::set_pct(0, 100);
    fan::set_pct(1, 100);
    temp::detect();
    asic::uart_init();
    ESP_LOGI(TAG, "%s: %d x %s", AF_BOARD_NAME, AF_NUM_ASICS, asic::MODEL);

#if AF_DETECT_REV7
    if (tps546::detect()) {
        s_rev7 = true;
        s_limits = REV7_LIMITS;
        ESP_LOGI(TAG, "rev7 board (TPS546) - limits %u MHz / %.0f W", s_limits.max_freq_mhz, s_limits.max_power_w);
    }
#endif
#if AF_DETECT_TPS53667
    // the strap has a pull-down: boards before rev3.0 read low (TPS53647)
    gpio_reset_pin(PIN_VR_DETECT);
    gpio_set_direction(PIN_VR_DETECT, GPIO_MODE_INPUT);
    gpio_set_pull_mode(PIN_VR_DETECT, GPIO_PULLDOWN_ONLY);
    vTaskDelay(pdMS_TO_TICKS(1));
    if (gpio_get_level(PIN_VR_DETECT)) {
        s_phases = 6;
        s_imax = 240;
        s_ifault = 235;
        s_vr_offset = 8;
        s_limits.max_power_w = CONFIG_AF_G67_MAX_POWER_W;
        s_limits.max_freq_mhz = CONFIG_AF_G67_MAX_FREQ_MHZ;
        // the stock firmware's 6-phase starting point; only while the settings
        // still hold the 4-phase defaults, a point the user picked is kept
        Settings &c = settings::get();
        if (c.freq_mhz == G67_4PH_FREQ && c.core_mv == G67_4PH_MV) {
            c.freq_mhz = 700;
            c.core_mv = 1210;
        }
        ESP_LOGI(TAG, "TPS53667 board (6 phases) - limits %u MHz / %.0f W", s_limits.max_freq_mhz, s_limits.max_power_w);
    } else {
        ESP_LOGI(TAG, "TPS53647 board (4 phases) - limits %u MHz / %.0f W", s_limits.max_freq_mhz, s_limits.max_power_w);
    }
#endif
    s_die_sensors = tmp451::start();
#ifdef AF_NO_MUX_MAX_FREQ
    if (s_die_sensors == 0) {
        // the QX is told apart from a NerdQaxe++ by its die sensor mux
        s_limits.max_freq_mhz = AF_NO_MUX_MAX_FREQ;
        s_max_mv = AF_NO_MUX_MAX_MV;
        ESP_LOGE(TAG, "no die temperature mux: not a NerdQX? limited to %u MHz / %u mV", AF_NO_MUX_MAX_FREQ,
                 AF_NO_MUX_MAX_MV);
    }
#endif
    g_state.board_rev7 = s_rev7;
    // without a working chip sensor there is no over-temperature protection
    if (temp::count() == 0 && s_die_sensors == 0) return Error::NoTempSensor;
    return Error::None;
}

static Error power_up_once(uint16_t freq, uint16_t mv, bool last_try);

// A chip that misses the count still hashes, but without an address of its own it
// shares chip 1's: repeated nonces and a nonsense hash counter. Seen when powering
// back up after a cut; a fresh power cycle finds all of them again.
#define POWER_UP_TRIES 3

Error power_up(uint16_t freq, uint16_t mv)
{
    Guard g;
    for (int i = 1;; i++) {
        Error e = power_up_once(freq, mv, i == POWER_UP_TRIES);
        bool short_chain = e == Error::ChipMissing || (e == Error::None && g_state.chips_detected < AF_NUM_ASICS);
        if (!short_chain || i == POWER_UP_TRIES) return e;
        ESP_LOGW(TAG, "%d of %d chips answered: power cycling (try %d of %d)", g_state.chips_detected, AF_NUM_ASICS,
                 i + 1, POWER_UP_TRIES);
        shutdown();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

static Error power_up_once(uint16_t freq, uint16_t mv, bool last_try)
{
    freq = clamp_freq(freq);
    mv = clamp_mv(mv);

    read_telemetry();
    if (!g_state.temp_ok || !g_state.vr_temp_ok) {
        ESP_LOGE(TAG, "temperature sensor not answering, refusing to power up");
        return Error::NoTempSensor;
    }

    gpio_set_level(PIN_VR_EN, 0);
    gpio_set_level(PIN_LDO_EN, 0);
    gpio_set_level(PIN_ASIC_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(250));

    gpio_set_level(PIN_LDO_EN, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    uint16_t init_mv = mv > AF_INIT_MV ? mv : AF_INIT_MV;
    if (s_rev7) {
        // TPS546: EN first, then configure; the output turns on with the first VOUT
        gpio_set_level(PIN_VR_EN, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
        if (!tps546::init(tps546::NERDQAXE_REV7) || !tps546::set_core(init_mv / 1000.0f)) {
            tps546::off();
            gpio_set_level(PIN_VR_EN, 0);
            gpio_set_level(PIN_LDO_EN, 0);
            return Error::NoRegulator;
        }
    } else {
        if (!tps53647::init(s_phases, s_imax, s_ifault)) {
            gpio_set_level(PIN_LDO_EN, 0);
            return Error::NoRegulator;
        }
#if AF_DETECT_TPS53667
        // phases and current limits come from the GPIO3 strap: a 6-phase setup on a
        // 4-phase TPS53647 (or the reverse) would leave the regulator unprotected
        if (tps53647::is_tps53667() != (s_phases == 6)) {
            ESP_LOGE(TAG, "GPIO3 strap says %s but the regulator is a %s: not powering up",
                     s_phases == 6 ? "TPS53667" : "TPS53647", tps53647::is_tps53667() ? "TPS53667" : "TPS53647");
            gpio_set_level(PIN_LDO_EN, 0);
            return Error::NoRegulator;
        }
#endif
        gpio_set_level(PIN_VR_EN, 1);
        tps53647::set_vout(init_mv / 1000.0f);
    }
    s_mv = init_mv;
    s_powered = true;
    vTaskDelay(pdMS_TO_TICKS(500));

    gpio_set_level(PIN_ASIC_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(250));

    int chips = asic::init(freq, AF_NUM_ASICS, START_DIFFICULTY);
    g_state.chips_detected = chips;
    if (chips == 0) {
        shutdown();
        return Error::NoChips;
    }
    if (chips != AF_NUM_ASICS && !last_try) return Error::None;  // power_up() cycles and tries again
    // rev7 stacks the chips in two series domains: a missing chip overvolts its partner
    if (s_rev7 && chips != AF_NUM_ASICS) {
        ESP_LOGE(TAG, "only %d of %d chips answered: not powering a broken stack", chips, AF_NUM_ASICS);
        shutdown();
        return Error::ChipMissing;
    }
    tmp451::enable(true);
    asic::switch_to_fast_baud();
    vTaskDelay(pdMS_TO_TICKS(500));

    set_core_mv(mv);
    s_freq = freq;
    g_state.freq_mhz = freq;
    g_state.expected_ghs = (freq * chips + asic::offset_total()) * (float) asic::SMALL_CORES / 1000.0f;
    return Error::None;
}

void shutdown()
{
    Guard g;
    ESP_LOGW(TAG, "shutting down ASIC power");
    tmp451::enable(false);
    if (s_rev7) tps546::off();
    gpio_set_level(PIN_VR_EN, 0);
    vTaskDelay(pdMS_TO_TICKS(500));
    gpio_set_level(PIN_LDO_EN, 0);
    gpio_set_level(PIN_ASIC_RST, 0);
    s_powered = false;
    s_mv = 0;
    g_state.core_mv = 0;
    // telemetry stops updating while off; don't leave the last readings on screen
    g_state.pin = g_state.iin = g_state.vout = g_state.iout = 0;
    fan::set_pct(0, 100);
    fan::set_pct(1, 100);
}

bool set_core_mv(uint16_t mv)
{
    Guard g;
    if (!s_powered) return false;
    mv = clamp_mv(mv);
    bool ok = s_rev7 ? tps546::set_core(mv / 1000.0f) : tps53647::set_vout(mv / 1000.0f);
    if (!ok) return false;
    s_mv = mv;
    g_state.core_mv = mv;
    return true;
}

bool set_freq_mhz(uint16_t mhz)
{
    Guard g;
    if (!s_powered) return false;
    mhz = clamp_freq(mhz);
    if (!asic::set_frequency(mhz)) return false;
    s_freq = mhz;
    g_state.freq_mhz = mhz;
    g_state.expected_ghs = (mhz * g_state.chips_detected + asic::offset_total()) * (float) asic::SMALL_CORES / 1000.0f;
    return true;
}

uint16_t core_mv() { return s_mv; }
uint16_t freq_mhz() { return s_freq; }
bool is_powered() { return s_powered; }

void read_telemetry()
{
    // The board has 1-3 TMP1075s near the chips; chips without their own sensor
    // show the nearest one, flagged. The NerdQX and NerdOctaxe-g also read each
    // die through a TMP451 mux. Those diodes only work while the chips are
    // powered, so before power-up (and if a die reading goes missing) the board
    // sensors stand in. A failed read clears temp_ok; the monitor cuts power if
    // that persists.
    float tmax = 0;
    int n = temp::count();
    bool ok = n > 0 || s_die_sensors > 0;
    float last = NAN;
    const float proxy_offset = s_rev7 ? REV7_DIE_TEMP_OFFSET : AF_PROXY_DIE_OFFSET;
    // each board sensor once per tick (the bus is shared with the TMP451 reader);
    // the hottest stands in for a chip whose die reading is missing
    float area[4];
    float board_max = NAN;
    for (int k = 0; k < n && k < 4; k++) {
        area[k] = temp::asic_area(k);
        if (!isnan(area[k])) area[k] += proxy_offset;
        if (!isnan(area[k]) && (isnan(board_max) || area[k] > board_max)) board_max = area[k];
    }
    for (int i = 0; i < AF_NUM_ASICS; i++) {
        float t = i < s_die_sensors ? tmp451::chip(i) : NAN;
        bool own = !isnan(t);
        if (!own && i < n && i < 4) {
            t = area[i];
            own = s_die_sensors == 0;  // a board sensor is only "own" on boards without die sensors
            last = t;
        } else if (!own) {
            t = s_die_sensors > 0 ? board_max : last;
        }
        if (isnan(t)) {
            ok = false;
            t = 0;
        }
        g_state.chip_temp[i] = t;
        g_state.chip_temp_own[i] = own;
        if (t > tmax) tmax = t;
    }
    g_state.chip_temp_max = tmax;
    g_state.temp_ok = ok;
    g_state.fan_rpm[0] = fan::rpm(0);
    g_state.fan_rpm[1] = fan::rpm(1);
    // TPS53647 boards measure the regulator with the TMP1075 on its back side
    // (the HX-6 has none there and reads the controller), the TPS546 reports its
    // own die temperature
#if AF_VR_TEMP_FROM_CONTROLLER
    float vr = tps53647::temperature();
#else
    float vr = s_rev7 ? tps546::temperature() : temp::vr_board() + s_vr_offset;
#endif
    g_state.vr_temp_ok = !isnan(vr);
    g_state.vr_temp = g_state.vr_temp_ok ? vr : 0;
    if (!s_powered) return;
    if (s_rev7) {
        // no input-side sensing on rev7: output power stands in for input power
        g_state.vin = tps546::vin();
        float v = tps546::vout();
        if (!isnan(v)) g_state.vout = v;  // a failed read keeps the last value
        g_state.iout = tps546::iout();
        float p = tps546::pout();
        if (!isnan(p)) g_state.pin = p;
        g_state.iin = g_state.vin > 1 ? g_state.pin / g_state.vin : 0;
        return;
    }
    g_state.vin = tps53647::vin();
    g_state.iin = tps53647::iin();
    g_state.pin = tps53647::pin();
    float v = tps53647::vout();
    if (!isnan(v)) g_state.vout = v;  // a failed read keeps the last value
    g_state.iout = tps53647::iout();
}

Error check_faults()
{
    if (!s_powered) return Error::None;
    uint8_t sb = s_rev7 ? tps546::status_byte() : tps53647::status_byte();
    uint8_t si = s_rev7 ? tps546::status_iout() : tps53647::status_iout();
    uint8_t sv = s_rev7 ? tps546::status_vout() : tps53647::status_vout();
    uint8_t sn = s_rev7 ? tps546::status_input() : tps53647::status_input();
    uint8_t st = s_rev7 ? tps546::status_temp() : tps53647::status_temp();
    if (sb == 0xff && si == 0xff && sv == 0xff && sn == 0xff && st == 0xff) return Error::PsuFault;
    if (si != 0xff && (si & 0x80)) return Error::OverCurrent;
    if (sv != 0xff && (sv & 0x90)) return Error::VoutFault;
    if (st != 0xff && (st & 0x80)) return Error::VrOverTemp;
    if (sn != 0xff && (sn & 0x94)) return Error::PsuFault;
    return Error::None;
}

const char *error_text(Error e)
{
    switch (e) {
    case Error::None: return "OK";
    case Error::NoRegulator: return "Voltage regulator not found";
    case Error::NoTempSensor: return "Temperature sensor not responding";
    case Error::NoChips: return NO_CHIPS_TEXT;
    case Error::ChipMissing: return "A chip in a stacked voltage domain didn't answer";
    case Error::PsuFault: return "PSU / input voltage fault";
    case Error::OverCurrent: return "Core over-current";
    case Error::VoutFault: return "Core voltage fault";
    case Error::VrOverTemp: return "Regulator over-temperature";
    }
    return "?";
}

} // namespace board

#endif // AF_NQ_FAMILY
