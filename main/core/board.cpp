#include "board.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "asic/bm1370.h"
#include "core/state.h"
#include "hw/i2c_bus.h"
#include "hw/sensors.h"
#include "hw/tps53647.h"

static const char *TAG = "board";

#define PIN_VR_EN GPIO_NUM_10
#define PIN_LDO_EN GPIO_NUM_13
#define PIN_ASIC_RST GPIO_NUM_1

#define NUM_PHASES 3
#define IMAX_A (NUM_PHASES * 30)
#define IFAULT_A (IMAX_A + 5)
#define INIT_MV 1200
#define START_DIFFICULTY 2048
#define TPS546_ADDR 0x24  // rev7 regulator, not supported by this firmware

// The TMP1075s sit on the PCB next to the chips; the die runs ~10 C hotter.
#define DIE_TEMP_OFFSET 10.0f

static bool s_powered = false;
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

uint16_t clamp_mv(int mv)
{
    if (mv < CONFIG_AF_MIN_CORE_MV) mv = CONFIG_AF_MIN_CORE_MV;
    if (mv > CONFIG_AF_MAX_CORE_MV) mv = CONFIG_AF_MAX_CORE_MV;
    return (uint16_t) mv;
}

uint16_t clamp_freq(int mhz)
{
    if (mhz < 400) mhz = 400;
    if (mhz > CONFIG_AF_MAX_FREQ_MHZ) mhz = CONFIG_AF_MAX_FREQ_MHZ;
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
    bm1370::uart_init();

    if (i2c_bus::probe(TPS546_ADDR)) {
        ESP_LOGE(TAG, "rev7 board (TPS546) detected - not supported, staying powered down");
        return Error::UnsupportedRev7;
    }
    return Error::None;
}

Error power_up(uint16_t freq, uint16_t mv)
{
    Guard g;
    freq = clamp_freq(freq);
    mv = clamp_mv(mv);

    gpio_set_level(PIN_VR_EN, 0);
    gpio_set_level(PIN_LDO_EN, 0);
    gpio_set_level(PIN_ASIC_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(250));

    gpio_set_level(PIN_LDO_EN, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    if (!tps53647::init(NUM_PHASES, IMAX_A, IFAULT_A)) {
        gpio_set_level(PIN_LDO_EN, 0);
        return Error::NoRegulator;
    }
    gpio_set_level(PIN_VR_EN, 1);
    uint16_t init_mv = mv > INIT_MV ? mv : INIT_MV;
    tps53647::set_vout(init_mv / 1000.0f);
    s_mv = init_mv;
    s_powered = true;
    vTaskDelay(pdMS_TO_TICKS(500));

    gpio_set_level(PIN_ASIC_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(250));

    int chips = bm1370::init(freq, AF_NUM_ASICS, START_DIFFICULTY);
    g_state.chips_detected = chips;
    if (chips == 0) {
        shutdown();
        return Error::NoChips;
    }
    bm1370::switch_to_fast_baud();
    vTaskDelay(pdMS_TO_TICKS(500));

    set_core_mv(mv);
    s_freq = freq;
    g_state.freq_mhz = freq;
    g_state.expected_ghs = freq * (float) bm1370::SMALL_CORES * chips / 1000.0f;
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
    if (!bm1370::set_frequency(mhz)) return false;
    s_freq = mhz;
    g_state.freq_mhz = mhz;
    g_state.expected_ghs = mhz * (float) bm1370::SMALL_CORES * g_state.chips_detected / 1000.0f;
    return true;
}

uint16_t core_mv() { return s_mv; }
uint16_t freq_mhz() { return s_freq; }
bool is_powered() { return s_powered; }

void read_telemetry()
{
    // The BM1370 has no readable die sensor; the board has 1-3 TMP1075s near the
    // chips. Chips without their own sensor show the nearest one, flagged.
    float tmax = 0;
    int n = temp::count();
    float last = 0;
    for (int i = 0; i < AF_NUM_ASICS; i++) {
        bool own = i < n;
        float t = own ? temp::asic_area(i) : last;
        if (own && t > 0) {
            t += DIE_TEMP_OFFSET;
            last = t;
        }
        g_state.chip_temp[i] = t;
        g_state.chip_temp_own[i] = own;
        if (t > tmax) tmax = t;
    }
    g_state.chip_temp_max = tmax;
    g_state.fan_rpm[0] = fan::rpm(0);
    g_state.fan_rpm[1] = fan::rpm(1);
    // TPS53647 boards measure the regulator with the TMP1075 on its back side
    g_state.vr_temp = temp::vr_board();
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
    uint8_t sb = tps53647::status_byte();
    uint8_t si = tps53647::status_iout();
    uint8_t sv = tps53647::status_vout();
    uint8_t sn = tps53647::status_input();
    uint8_t st = tps53647::status_temp();
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
    case Error::UnsupportedRev7: return "Rev7 board (TPS546) not supported yet";
    case Error::NoChips: return "No BM1370 chips answered";
    case Error::PsuFault: return "PSU / input voltage fault";
    case Error::OverCurrent: return "Core over-current";
    case Error::VoutFault: return "Core voltage fault";
    case Error::VrOverTemp: return "Regulator over-temperature";
    }
    return "?";
}

} // namespace board
