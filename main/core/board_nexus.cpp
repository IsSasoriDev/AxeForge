// BitLuck Nexus S1 (board version 1010): 4x BM1373, TPS53647 regulator on 4 phases,
// EMC2302 fan controller (2 fans), TMP1075s at 0x48 (chips) and 0x49 (regulator),
// a T-Display-S3 panel and W5500 Ethernet (SPI 11/13/12/10, INT 2; unused here).
//
// BitLuck publishes binaries only, so the pins, power sequence and regulator
// settings below were read from the stock firmware (nexus.v1.0.2), which is
// built on shufps' NerdQAxe code. Apart from the power pins the layout is the
// NerdQaxe++'s: UART1 17/18, I2C 44/43, the same display pins.
//
// Experimental: tested on one board (450 MHz / 950 mV matches stock, 475 MHz /
// 975 mV ran clean). With no schematics the limits stay close to stock. Wi-Fi
// and pools are imported from the stock firmware's settings.
#include "sdkconfig.h"
#if CONFIG_AF_BOARD_NEXUS

#include "board.h"

#include <math.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "asic/bm13xx.h"
#include "core/state.h"
#include "hw/i2c_bus.h"
#include "hw/sensors.h"
#include "hw/tps53647.h"

static const char *TAG = "board";

// all active high, all driven low at boot by the stock firmware
#define PIN_VR_EN GPIO_NUM_21   // TPS53647 enable
#define PIN_LDO_EN GPIO_NUM_3   // a strapping pin: only driven after boot
#define PIN_ASIC_RST GPIO_NUM_1  // low = reset, high = run

#define NUM_PHASES 4
#define IMAX_A 180       // stock firmware; it draws ~130 A at 450 MHz / 0.95 V
#define IFAULT_A 225.0f
#define INIT_MV 950      // stock: chips enumerate at max(950 mV, target)
#define START_DIFFICULTY 1024
#define MIN_FREQ_MHZ 300

static bool s_powered = false;
static board::Limits s_limits = {CONFIG_AF_MAX_FREQ_MHZ, CONFIG_AF_MAX_POWER_W, CONFIG_AF_MAX_VR_TEMP,
                                 CONFIG_AF_CRIT_VR_TEMP};
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
    if (mhz < MIN_FREQ_MHZ) mhz = MIN_FREQ_MHZ;
    if (mhz > s_limits.max_freq_mhz) mhz = s_limits.max_freq_mhz;
    return (uint16_t) mhz;
}

Error init_hardware()
{
    s_lock = xSemaphoreCreateRecursiveMutex();
    // off, like the stock firmware's first step: a floating enable is the risk
    out_pin(PIN_VR_EN, 0);
    out_pin(PIN_LDO_EN, 0);
    out_pin(PIN_ASIC_RST, 0);
    if (i2c_bus::init() != ESP_OK) return Error::NoRegulator;
    fan::init();
    fan::set_pct(0, 100);
    fan::set_pct(1, 100);
    temp::detect();
    asic::uart_init();
    if (!tps53647::detect()) return Error::NoRegulator;
    // read-only look at the regulator before anything configures it
    float vin = 0, t = 0;
    if (tps53647::peek(&vin, &t)) ESP_LOGI(TAG, "TPS53647: vin %.2f V, controller %.1f C", vin, t);
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

    // the stock firmware's order and delays
    gpio_set_level(PIN_VR_EN, 0);
    gpio_set_level(PIN_LDO_EN, 0);
    gpio_set_level(PIN_ASIC_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(250));

    gpio_set_level(PIN_LDO_EN, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    uint16_t init_mv = mv > INIT_MV ? mv : INIT_MV;
    if (!tps53647::init(NUM_PHASES, IMAX_A, IFAULT_A)) {
        gpio_set_level(PIN_LDO_EN, 0);
        return Error::NoRegulator;
    }
    gpio_set_level(PIN_VR_EN, 1);
    tps53647::set_vout(init_mv / 1000.0f);
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
    gpio_set_level(PIN_VR_EN, 0);
    vTaskDelay(pdMS_TO_TICKS(500));
    gpio_set_level(PIN_LDO_EN, 0);
    gpio_set_level(PIN_ASIC_RST, 0);
    s_powered = false;
    s_mv = 0;
    g_state.core_mv = 0;
    g_state.pin = g_state.iin = g_state.vout = g_state.iout = 0;
    fan::set_pct(0, 100);
    fan::set_pct(1, 100);
}

bool set_core_mv(uint16_t mv)
{
    Guard g;
    if (!s_powered) return false;
    mv = clamp_mv(mv);
    if (!tps53647::set_vout(mv / 1000.0f)) return false;
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
    // one TMP1075 next to the chips: every chip shows it, only chip 0 owns it
    float t = temp::count() > 0 ? temp::asic_area(0) : NAN;
    bool ok = !isnan(t);
    for (int i = 0; i < AF_NUM_ASICS; i++) {
        g_state.chip_temp[i] = ok ? t : 0;
        g_state.chip_temp_own[i] = i == 0;
    }
    g_state.chip_temp_max = ok ? t : 0;
    g_state.temp_ok = ok;
    g_state.fan_rpm[0] = fan::rpm(0);
    g_state.fan_rpm[1] = fan::rpm(1);
    // The stock firmware shows the TMP1075 at the VR, which reads ~8.5 C below the
    // TPS53647's own sensor. The VR limits act on the hotter of the two.
    float vr = temp::vr_board();
    if (s_powered) {
        float own = tps53647::temperature();
        if (own > 0 && (isnan(vr) || own > vr)) vr = own;
    }
    g_state.vr_temp_ok = !isnan(vr);
    g_state.vr_temp = g_state.vr_temp_ok ? vr : 0;
    if (!s_powered) return;
    g_state.vin = tps53647::vin();
    g_state.iin = tps53647::iin();
    g_state.pin = tps53647::pin();
    g_state.vout = tps53647::vout();
    g_state.iout = tps53647::iout();
}

Error check_faults()
{
    if (!s_powered) return Error::None;
    uint8_t sb = tps53647::status_byte(), si = tps53647::status_iout(), sv = tps53647::status_vout();
    uint8_t sn = tps53647::status_input(), st = tps53647::status_temp();
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
    case Error::NoRegulator:
        return "Voltage regulator not found";
    case Error::NoTempSensor: return "Temperature sensor not responding";
    case Error::NoChips: return "No BM1373 chips answered";
    case Error::ChipMissing: return "A chip in a stacked voltage domain didn't answer";
    case Error::PsuFault: return "PSU / input voltage fault";
    case Error::OverCurrent: return "Core over-current";
    case Error::VoutFault: return "Core voltage fault";
    case Error::VrOverTemp: return "Regulator over-temperature";
    }
    return "?";
}

} // namespace board

#endif // CONFIG_AF_BOARD_NEXUS
