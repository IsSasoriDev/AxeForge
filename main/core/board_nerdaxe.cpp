// NerdAxe: 1x BM1366 on a LilyGo T-Display-S3. Core voltage from a TPS40305
// buck trimmed by a DS4432U current DAC, input power from an INA260, fan and
// board temperature from an EMC2101, core voltage read back on ADC1 ch1.
// The buck enable (GPIO10) is active LOW here, the opposite of the NerdQaxe++.
#include "sdkconfig.h"
#if CONFIG_AF_BOARD_NERDAXE

#include "board.h"

#include <math.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "asic/bm13xx.h"
#include "core/state.h"
#include "hw/ds4432u.h"
#include "hw/i2c_bus.h"
#include "hw/sensors.h"

static const char *TAG = "board";

#define PIN_PWR_EN GPIO_NUM_10  // LOW = buck on
#define PIN_ASIC_RST ((gpio_num_t) AF_PIN_ASIC_RST)
#define BUCK_ON 0
#define BUCK_OFF 1

#define START_DIFFICULTY 256
// EMC2101 internal sensor sits next to the chip; the die runs ~5 C hotter
// (same offset as the stock firmware)
#define DIE_TEMP_OFFSET 5.0f
#define MIN_VIN 4.3f  // 5 V board: below this the supply is sagging

static bool s_powered = false;
static const board::Limits s_limits = {CONFIG_AF_MAX_FREQ_MHZ, CONFIG_AF_MAX_POWER_W, CONFIG_AF_MAX_VR_TEMP,
                                       CONFIG_AF_CRIT_VR_TEMP};
static SemaphoreHandle_t s_lock;
static uint16_t s_mv = 0, s_freq = 0;

namespace {
struct Guard {
    Guard() { xSemaphoreTakeRecursive(s_lock, portMAX_DELAY); }
    ~Guard() { xSemaphoreGiveRecursive(s_lock); }
};
}

namespace board {

const Limits &limits() { return s_limits; }
bool is_rev7() { return false; }

uint16_t clamp_mv(int mv)
{
    if (mv < CONFIG_AF_MIN_CORE_MV) mv = CONFIG_AF_MIN_CORE_MV;
    if (mv > CONFIG_AF_MAX_CORE_MV) mv = CONFIG_AF_MAX_CORE_MV;
    return (uint16_t) mv;
}

uint16_t max_mv() { return CONFIG_AF_MAX_CORE_MV; }

uint16_t clamp_freq(int mhz)
{
    if (mhz < AF_MIN_FREQ_MHZ) mhz = AF_MIN_FREQ_MHZ;
    if (mhz > s_limits.max_freq_mhz) mhz = s_limits.max_freq_mhz;
    return (uint16_t) mhz;
}

Error init_hardware()
{
    s_lock = xSemaphoreCreateRecursiveMutex();
    // buck off and chip in reset before anything else
    gpio_reset_pin(PIN_PWR_EN);
    gpio_set_direction(PIN_PWR_EN, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_PWR_EN, BUCK_OFF);
    gpio_reset_pin(PIN_ASIC_RST);
    gpio_set_direction(PIN_ASIC_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_ASIC_RST, 0);

    if (i2c_bus::init() != ESP_OK) return Error::NoRegulator;
    fan::init();
    fan::set_pct(0, 100);
    temp::detect();
    ds4432u::init();
    asic::uart_init();

    if (!ds4432u::present()) return Error::NoRegulator;
    g_state.board_rev7 = false;
    if (temp::count() == 0) return Error::NoTempSensor;
    return Error::None;
}

Error power_up(uint16_t freq, uint16_t mv)
{
    Guard g;
    freq = clamp_freq(freq);
    mv = clamp_mv(mv);

    read_telemetry();
    if (!g_state.temp_ok) {
        ESP_LOGE(TAG, "temperature sensor not answering, refusing to power up");
        return Error::NoTempSensor;
    }

    gpio_set_level(PIN_PWR_EN, BUCK_OFF);
    gpio_set_level(PIN_ASIC_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(250));

    if (!ds4432u::set_vout(mv / 1000.0f)) return Error::NoRegulator;
    gpio_set_level(PIN_PWR_EN, BUCK_ON);
    s_mv = mv;
    g_state.core_mv = mv;
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

    s_freq = freq;
    g_state.freq_mhz = freq;
    g_state.expected_ghs = freq * (float) asic::SMALL_CORES * chips / 1000.0f;
    return Error::None;
}

void shutdown()
{
    Guard g;
    ESP_LOGW(TAG, "shutting down ASIC power");
    gpio_set_level(PIN_PWR_EN, BUCK_OFF);
    gpio_set_level(PIN_ASIC_RST, 0);
    s_powered = false;
    s_mv = 0;
    g_state.core_mv = 0;
    g_state.pin = g_state.iin = g_state.vout = g_state.iout = 0;
    fan::set_pct(0, 100);
}

bool set_core_mv(uint16_t mv)
{
    Guard g;
    if (!s_powered) return false;
    mv = clamp_mv(mv);
    if (!ds4432u::set_vout(mv / 1000.0f)) return false;
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
    float t = temp::asic_area(0);
    bool ok = !isnan(t);
    t = ok ? t + DIE_TEMP_OFFSET : 0;
    g_state.chip_temp[0] = t;
    g_state.chip_temp_own[0] = true;
    g_state.chip_temp_max = t;
    g_state.temp_ok = ok;
    // no separate regulator sensor on this board: the chip sensor covers both
    g_state.vr_temp_ok = ok;
    g_state.vr_temp = 0;
    g_state.fan_rpm[0] = fan::rpm(0);
    g_state.fan_rpm[1] = 0;

    float v, i, p;
    if (ds4432u::vin(v)) g_state.vin = v;
    if (!s_powered) return;
    if (ds4432u::iin(i)) g_state.iin = i;
    if (ds4432u::pin(p)) g_state.pin = p;
    // NAN when the ADC can't tell: the voltage-lost check skips it then
    g_state.vout = ds4432u::vcore();
    g_state.iout = 0;
}

Error check_faults()
{
    if (!s_powered) return Error::None;
    // no PMBus status here; a sagging 5 V input is the fault this board can see
    if (g_state.vin > 0.5f && g_state.vin < MIN_VIN) return Error::PsuFault;
    return Error::None;
}

const char *error_text(Error e)
{
    switch (e) {
    case Error::None: return "OK";
    case Error::NoRegulator: return "Voltage DAC (DS4432U) not found";
    case Error::NoTempSensor: return "Temperature sensor not responding";
    case Error::NoChips: return "No BM1366 chip answered";
    case Error::ChipMissing: return "A chip in a stacked voltage domain didn't answer";
    case Error::PsuFault: return "5 V input too low";
    case Error::OverCurrent: return "Core over-current";
    case Error::VoutFault: return "Core voltage fault";
    case Error::VrOverTemp: return "Regulator over-temperature";
    }
    return "?";
}

} // namespace board

#endif // CONFIG_AF_BOARD_NERDAXE
