// Bitaxe Supra Hex 701/702 (6x BM1368) and Ultra Hex 302/303 (6x BM1366): the
// chips in 3 series voltage domains, one TPS546D24 (PMBus on/off, no enable
// pin), EMC2302 fans, TMP1075 x2. The two boards differ only in the chip.
// Power-up order follows the Bitaxe reference firmware: regulator configured
// and switched on with the chips held in reset, then reset released.
#include "sdkconfig.h"
#include "core/board_config.h"
#if AF_HEX_FAMILY

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
#include "hw/tps546.h"

static const char *TAG = "board";

#define PIN_ASIC_RST ((gpio_num_t) AF_PIN_ASIC_RST)
#define INIT_MV 1150  // enumeration is reliable from here up
#define START_DIFFICULTY 2048
// The regulator only reports its output; fans, ESP32 and conversion losses
// come on top. Same allowance the reference firmware uses for each board.
#if CONFIG_AF_BOARD_ULTRAHEX
#define POWER_OFFSET_W 12.0f
#else
#define POWER_OFFSET_W 25.0f
#endif
// TMP1075s sit on the PCB next to the chips; the die runs ~10 C hotter.
#define DIE_TEMP_OFFSET 10.0f

static bool s_powered = false;
static const board::Limits s_limits = {CONFIG_AF_MAX_FREQ_MHZ, CONFIG_AF_MAX_POWER_W, CONFIG_AF_MAX_VR_TEMP,
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
    gpio_reset_pin(PIN_ASIC_RST);
    gpio_set_direction(PIN_ASIC_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_ASIC_RST, 0);

    if (i2c_bus::init() != ESP_OK) return Error::NoRegulator;
    fan::init();
    fan::set_pct(0, 100);
    fan::set_pct(1, 100);
    temp::detect();
    asic::uart_init();

    if (!tps546::detect()) return Error::NoRegulator;
    // after an OTA reboot the previous firmware may have left the output on
    tps546::off();
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
    if (!g_state.temp_ok || !g_state.vr_temp_ok) {
        ESP_LOGE(TAG, "temperature sensor not answering, refusing to power up");
        return Error::NoTempSensor;
    }

    gpio_set_level(PIN_ASIC_RST, 0);
    uint16_t init_mv = mv > INIT_MV ? mv : INIT_MV;
    if (!tps546::init(tps546::BITAXE_HEX) || !tps546::set_core(init_mv / 1000.0f)) {
        tps546::off();
        return Error::NoRegulator;
    }
    s_mv = init_mv;
    g_state.core_mv = init_mv;
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
    // The six chips sit in three series domains of two. With one missing, its
    // partner gets the domain's whole share of the stack voltage: never run that.
    if (chips != AF_NUM_ASICS) {
        ESP_LOGE(TAG, "only %d of %d chips answered: not powering a broken stack", chips, AF_NUM_ASICS);
        shutdown();
        return Error::ChipMissing;
    }
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
    tps546::off();
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
    if (!tps546::set_core(mv / 1000.0f)) return false;
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
    // Two board sensors for six chips: each chip shows the sensor of its half
    // of the board (flagged as shared). A failed read clears temp_ok.
    float tmax = 0;
    int n = temp::count();
    bool ok = n > 0;
    float area[2] = {0, 0};
    for (int s = 0; s < n && s < 2; s++) {
        float t = temp::asic_area(s);
        if (isnan(t)) {
            ok = false;
            t = 0;
        } else {
            t += DIE_TEMP_OFFSET;
        }
        area[s] = t;
        if (t > tmax) tmax = t;
    }
    for (int i = 0; i < AF_NUM_ASICS; i++) {
        g_state.chip_temp[i] = n ? area[i * n / AF_NUM_ASICS] : 0;
        g_state.chip_temp_own[i] = false;
    }
    g_state.chip_temp_max = tmax;
    g_state.temp_ok = ok;
    g_state.fan_rpm[0] = fan::rpm(0);
    g_state.fan_rpm[1] = fan::rpm(1);
    float vr = tps546::temperature();
    g_state.vr_temp_ok = !isnan(vr);
    g_state.vr_temp = g_state.vr_temp_ok ? vr : 0;
    if (!s_powered) return;
    g_state.vin = tps546::vin();
    float v = tps546::vout();
    if (!isnan(v)) g_state.vout = v;  // a failed read keeps the last value
    g_state.iout = tps546::iout();
    float p = tps546::pout();
    if (!isnan(p)) g_state.pin = p + POWER_OFFSET_W;
    g_state.iin = g_state.vin > 1 ? g_state.pin / g_state.vin : 0;
}

void log_regulator(const char *why) { tps546::log_status(why); }

Error check_faults()
{
    if (!s_powered) return Error::None;
    uint8_t sb = tps546::status_byte();
    uint8_t si = tps546::status_iout();
    uint8_t sv = tps546::status_vout();
    uint8_t sn = tps546::status_input();
    uint8_t st = tps546::status_temp();
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
#if AF_ASIC_BM1366
    case Error::NoChips: return "No BM1366 chips answered";
#else
    case Error::NoChips: return "No BM1368 chips answered";
#endif
    case Error::ChipMissing: return "A chip in a stacked voltage domain didn't answer";
    case Error::PsuFault: return "PSU / input voltage fault";
    case Error::OverCurrent: return "Core over-current";
    case Error::VoutFault: return "Core voltage fault";
    case Error::VrOverTemp: return "Regulator over-temperature";
    }
    return "?";
}

} // namespace board

#endif // AF_HEX_FAMILY
