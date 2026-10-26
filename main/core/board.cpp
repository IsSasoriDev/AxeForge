// NerdQaxe++ (rev5.1/rev6: TPS53647, rev7: TPS546 stack). The Supra Hex lives in
// board_suprahex.cpp; exactly one of the two is compiled.
#include "sdkconfig.h"
#if CONFIG_AF_BOARD_NERDQAXE

#include "board.h"

#include <math.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "asic/bm13xx.h"
#include "core/state.h"
#include "hw/i2c_bus.h"
#include "hw/sensors.h"
#include "hw/tps53647.h"
#include "hw/tps546.h"

static const char *TAG = "board";

#define PIN_VR_EN GPIO_NUM_10
#define PIN_LDO_EN GPIO_NUM_13
#define PIN_ASIC_RST GPIO_NUM_1

#define NUM_PHASES 3
#define IMAX_A (NUM_PHASES * 30)
#define IFAULT_A (IMAX_A + 5)
#define INIT_MV 1200
#define START_DIFFICULTY 2048

// The TMP1075s sit on the PCB next to the chips; the die runs ~10 C hotter.
// Only rev7 adds this; rev <= 6 reports the raw sensor (PR #6).
#define REV7_DIE_TEMP_OFFSET 10.0f

static bool s_powered = false;
static bool s_rev7 = false;
static board::Limits s_limits = {CONFIG_AF_MAX_FREQ_MHZ, CONFIG_AF_MAX_POWER_W, CONFIG_AF_MAX_VR_TEMP,
                                 CONFIG_AF_CRIT_VR_TEMP};
static const board::Limits REV7_LIMITS = {CONFIG_AF_REV7_MAX_FREQ_MHZ, CONFIG_AF_REV7_MAX_POWER_W,
                                          CONFIG_AF_REV7_MAX_VR_TEMP, CONFIG_AF_REV7_CRIT_VR_TEMP};
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
    if (mv > CONFIG_AF_MAX_CORE_MV) mv = CONFIG_AF_MAX_CORE_MV;
    return (uint16_t) mv;
}

uint16_t clamp_freq(int mhz)
{
    if (mhz < 400) mhz = 400;
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

    if (tps546::detect()) {
        s_rev7 = true;
        s_limits = REV7_LIMITS;
        ESP_LOGI(TAG, "rev7 board (TPS546) - limits %u MHz / %.0f W", s_limits.max_freq_mhz, s_limits.max_power_w);
    }
    g_state.board_rev7 = s_rev7;
    // without a working chip sensor there is no over-temperature protection
    if (temp::count() == 0) return Error::NoTempSensor;
    return Error::None;
}

Error power_up(uint16_t freq, uint16_t mv)
{
    Guard g;
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

    uint16_t init_mv = mv > INIT_MV ? mv : INIT_MV;
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
        if (!tps53647::init(NUM_PHASES, IMAX_A, IFAULT_A)) {
            gpio_set_level(PIN_LDO_EN, 0);
            return Error::NoRegulator;
        }
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
    asic::switch_to_fast_baud();
    vTaskDelay(pdMS_TO_TICKS(500));

    set_core_mv(mv);
    s_freq = freq;
    g_state.freq_mhz = freq;
    g_state.expected_ghs = freq * (float) asic::SMALL_CORES * chips / 1000.0f;
    return Error::None;
}

void shutdown()
{
    Guard g;
    ESP_LOGW(TAG, "shutting down ASIC power");
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
    g_state.expected_ghs = mhz * (float) asic::SMALL_CORES * g_state.chips_detected / 1000.0f;
    return true;
}

uint16_t core_mv() { return s_mv; }
uint16_t freq_mhz() { return s_freq; }
bool is_powered() { return s_powered; }

void read_telemetry()
{
    // The BM1370 has no readable die sensor; the board has 1-3 TMP1075s near the
    // chips. Chips without their own sensor show the nearest one, flagged.
    // A failed read clears temp_ok; the monitor cuts power if that persists.
    float tmax = 0;
    int n = temp::count();
    bool ok = n > 0;
    float last = 0;
    for (int i = 0; i < AF_NUM_ASICS; i++) {
        bool own = i < n;
        float t = own ? temp::asic_area(i) : last;
        if (own) {
            if (isnan(t)) {
                ok = false;
                t = 0;
            } else {
                if (s_rev7) t += REV7_DIE_TEMP_OFFSET;
            }
            last = t;
        }
        g_state.chip_temp[i] = t;
        g_state.chip_temp_own[i] = own;
        if (t > tmax) tmax = t;
    }
    g_state.chip_temp_max = tmax;
    g_state.temp_ok = ok;
    g_state.fan_rpm[0] = fan::rpm(0);
    g_state.fan_rpm[1] = fan::rpm(1);
    // TPS53647 boards measure the regulator with the TMP1075 on its back side,
    // the TPS546 reports its own die temperature
    float vr = s_rev7 ? tps546::temperature() : temp::vr_board();
    g_state.vr_temp_ok = !isnan(vr);
    g_state.vr_temp = g_state.vr_temp_ok ? vr : 0;
    if (!s_powered) return;
    if (s_rev7) {
        // no input-side sensing on rev7: output power stands in for input power
        g_state.vin = tps546::vin();
        g_state.vout = tps546::vout();
        g_state.iout = tps546::iout();
        g_state.pin = tps546::pout();
        g_state.iin = g_state.vin > 1 ? g_state.pin / g_state.vin : 0;
        return;
    }
    g_state.vin = tps53647::vin();
    g_state.iin = tps53647::iin();
    g_state.pin = tps53647::pin();
    g_state.vout = tps53647::vout();
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
    case Error::NoChips: return "No BM1370 chips answered";
    case Error::PsuFault: return "PSU / input voltage fault";
    case Error::OverCurrent: return "Core over-current";
    case Error::VoutFault: return "Core voltage fault";
    case Error::VrOverTemp: return "Regulator over-temperature";
    }
    return "?";
}

} // namespace board

#endif // CONFIG_AF_BOARD_NERDQAXE
