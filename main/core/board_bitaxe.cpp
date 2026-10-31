// The Bitaxes with one regulator (or one stack of them):
//  - Max (1x BM1397): the 5 V DS4432U path, its chip diode on the EMC2101.
//  - Supra (1x BM1368) and Ultra (1x BM1366): rev 402/403 and 207 run a TPS546 on
//    5 V; rev 400/401 and 201-205 the 5 V DS4432U + TPS40305 path (as the NerdAxe),
//    enabled by GPIO10 low.
//  - Gamma (1x BM1370) and Gamma Duo (2x BM1370 in parallel): TPS546 on 5 V.
//  - GT (2x BM1370 in parallel): two stacked TPS546s on 12 V.
//  - Naja Duo (2x BM1373, 2 series domains) and Gamma Hex (6x BM1370, 2 series
//    domains of 3): stacked TPS546s on 12 V, enabled by GPIO10 high.
// The regulator is found at boot, so one image covers every revision of a board.
// Fan and temperatures come from one EMC2101 or EMC2103: the controller's own
// sensor before power-up, the chip diodes once powered. With fewer diodes than
// chips, a chip shows the diode nearest to it, marked as shared.
// Power-up order follows the Bitaxe reference firmware: regulator configured and
// switched on with the chips held in reset, then reset released.
#include "core/board_config.h"
#if AF_BITAXE_FAMILY

#include "board.h"

#include <math.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "asic/bm13xx.h"
#include "core/gates.h"
#include "core/state.h"
#include "hw/ds4432u.h"
#include "hw/i2c_bus.h"
#include "hw/sensors.h"
#include "hw/tps546.h"

static const char *TAG = "board";

#define PIN_ASIC_RST ((gpio_num_t) AF_PIN_ASIC_RST)
#define PIN_ASIC_EN GPIO_NUM_10  // Supra 400/401: buck enable, active low; Naja: active high
#define START_DIFFICULTY AF_ASIC_DIFF_MAX
// The controller's own sensor sits next to the chip; the die runs ~5 C hotter
// (reference firmware offset for the boards that use it as the chip reading).
#define BOARD_TO_DIE_C 5.0f
// A diode needs a conversion or two after power-up before it reads
#define DIODE_GRACE_US (5 * 1000000LL)
#define MIN_VIN_5V 4.3f  // 5 V boards: below this the supply is sagging
#if CONFIG_AF_BOARD_NAJA
#define TPS_PROFILE tps546::NAJA_DUO
#elif CONFIG_AF_BOARD_BITAXE_GT
#define TPS_PROFILE tps546::GAMMA_TURBO
#elif CONFIG_AF_BOARD_GAMMA_HEX
#define TPS_PROFILE tps546::GAMMA_HEX
#else
#define TPS_PROFILE tps546::BITAXE_SINGLE
#endif
// reference allowance for fans, ESP32 and conversion losses, added to the regulator's output
#ifdef AF_POWER_OFFSET_W
#define POWER_OFFSET_W AF_POWER_OFFSET_W
#else
#define POWER_OFFSET_W 5.0f
#endif
#if AF_EMC2103
#define CHIP_DIODES 2
#else
#define CHIP_DIODES 1
#endif

static bool s_powered = false;
static bool s_tps = false;  // TPS546 board; otherwise the DS4432U path (Supra 400/401)
static int64_t s_powered_at = 0;
static const board::Limits s_limits = {CONFIG_AF_MAX_FREQ_MHZ, CONFIG_AF_MAX_POWER_W, CONFIG_AF_MAX_VR_TEMP,
                                       CONFIG_AF_CRIT_VR_TEMP};
static SemaphoreHandle_t s_lock;  // monitor and tuner both drive power
static uint16_t s_mv = 0, s_freq = 0;

namespace {
struct Guard {
    Guard() { xSemaphoreTakeRecursive(s_lock, portMAX_DELAY); }
    ~Guard() { xSemaphoreGiveRecursive(s_lock); }
};
}

// The enable pin, where the board has one. `on` = core power allowed.
static void enable(bool on)
{
#if AF_ASIC_EN_ACTIVE_HIGH
    gpio_set_level(PIN_ASIC_EN, on ? 1 : 0);
#else
    if (!s_tps) gpio_set_level(PIN_ASIC_EN, on ? 0 : 1);  // DS4432U boards: LOW = buck on
#endif
}

static bool set_vout(float volts) { return s_tps ? tps546::set_core(volts) : ds4432u::set_vout(volts); }

namespace board {

const Limits &limits() { return s_limits; }
bool is_rev7() { return false; }

uint16_t clamp_mv(int mv)
{
    if (mv < CONFIG_AF_MIN_CORE_MV) mv = CONFIG_AF_MIN_CORE_MV;
    if (mv > gates::max_mv()) mv = gates::max_mv();  // stock limit, or the envelope when overclocked
    return (uint16_t) mv;
}

uint16_t max_mv() { return CONFIG_AF_MAX_CORE_MV; }

uint16_t clamp_freq(int mhz)
{
    if (mhz < AF_MIN_FREQ_MHZ) mhz = AF_MIN_FREQ_MHZ;
    if (mhz > gates::max_freq()) mhz = gates::max_freq();
    return (uint16_t) mhz;
}

Error init_hardware()
{
    s_lock = xSemaphoreCreateRecursiveMutex();
    gpio_reset_pin(PIN_ASIC_RST);
    gpio_set_direction(PIN_ASIC_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_ASIC_RST, 0);

    if (i2c_bus::init() != ESP_OK) return Error::NoRegulator;
    s_tps = tps546::detect();
#if AF_ASIC_EN_ACTIVE_HIGH
    gpio_reset_pin(PIN_ASIC_EN);
    gpio_set_direction(PIN_ASIC_EN, GPIO_MODE_OUTPUT);
    enable(false);
#else
    if (!s_tps) {
        // only the DS4432U boards wire GPIO10 to the buck; leave it alone otherwise
        gpio_reset_pin(PIN_ASIC_EN);
        gpio_set_direction(PIN_ASIC_EN, GPIO_MODE_OUTPUT);
        enable(false);
    }
#endif
    fan::init();
    fan::set_pct(0, 100);
    temp::detect();
    asic::uart_init();

    if (s_tps) {
        tps546::off();  // after an OTA reboot the previous firmware may have left it on
        ESP_LOGI(TAG, "%s: %d x %s, TPS546 regulator", AF_BOARD_NAME, AF_NUM_ASICS, asic::MODEL);
    } else {
#if AF_HAS_DS4432U
        if (!ds4432u::present()) return Error::NoRegulator;
        ds4432u::init();
        ESP_LOGI(TAG, "%s: %d x %s, DS4432U regulator", AF_BOARD_NAME, AF_NUM_ASICS, asic::MODEL);
#else
        return Error::NoRegulator;  // every other board has a TPS546
#endif
    }
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
    enable(false);
    vTaskDelay(pdMS_TO_TICKS(250));
    if (s_tps) {
        enable(true);  // the Naja's regulators need their enable before PMBus config
        vTaskDelay(pdMS_TO_TICKS(20));
        if (!tps546::init(TPS_PROFILE) || !tps546::set_core(mv / 1000.0f)) {
            tps546::off();
            enable(false);
            return Error::NoRegulator;
        }
    } else {
        if (!ds4432u::set_vout(mv / 1000.0f)) return Error::NoRegulator;
        enable(true);
    }
    s_mv = mv;
    g_state.core_mv = mv;
    s_powered = true;
    s_powered_at = esp_timer_get_time();
    vTaskDelay(pdMS_TO_TICKS(500));

    gpio_set_level(PIN_ASIC_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(250));

    int chips = asic::init(freq, AF_NUM_ASICS, START_DIFFICULTY);
    g_state.chips_detected = chips;
    if (chips == 0) {
        shutdown();
        return Error::NoChips;
    }
#if AF_VOLTAGE_DOMAINS > 1
    // series domains: a missing chip leaves its partner with the whole domain's share
    if (chips != AF_NUM_ASICS) {
        ESP_LOGE(TAG, "only %d of %d chips answered: not powering a broken stack", chips, AF_NUM_ASICS);
        shutdown();
        return Error::ChipMissing;
    }
#endif
    asic::switch_to_fast_baud();
    vTaskDelay(pdMS_TO_TICKS(500));

    s_freq = freq;
    g_state.freq_mhz = freq;
    g_state.expected_ghs = (freq * chips + asic::offset_total()) * (float) asic::SMALL_CORES / 1000.0f;
    return Error::None;
}

void shutdown()
{
    Guard g;
    ESP_LOGW(TAG, "shutting down ASIC power");
    if (s_tps) tps546::off();
    enable(false);
    gpio_set_level(PIN_ASIC_RST, 0);
    s_powered = false;
    s_mv = 0;
    g_state.core_mv = 0;
    // telemetry stops updating while off; don't leave the last readings on screen
    g_state.pin = g_state.iin = g_state.vout = g_state.iout = 0;
    fan::set_pct(0, 100);
}

bool set_core_mv(uint16_t mv)
{
    Guard g;
    if (!s_powered) return false;
    mv = clamp_mv(mv);
    if (!set_vout(mv / 1000.0f)) return false;
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
    // The controller's own sensor (next to the chip) stands in until the chips
    // are powered and their diodes read; on the Supra 400/401 it is the chip
    // reading, as in the reference firmware. Once powered past the grace period,
    // a diode that stops answering is a lost sensor, not a reason to fall back.
    float board_t = temp::asic_area(0);
    bool board_ok = !isnan(board_t);
#if AF_DS4432U_DIODE
    const bool has_diode = true;
#else
    const bool has_diode = s_tps;
#endif
    bool diodes = has_diode && s_powered && esp_timer_get_time() - s_powered_at > DIODE_GRACE_US;
    float tmax = 0;
    bool ok = board_ok;
    for (int i = 0; i < AF_NUM_ASICS; i++) {
        float t;
        bool own;
        if (diodes) {
            // chips are split evenly over the diodes, in chain order
            t = temp::chip_diode(i * CHIP_DIODES / AF_NUM_ASICS);
            own = AF_NUM_ASICS <= CHIP_DIODES;
            if (isnan(t)) ok = false;
        } else {
            t = board_ok ? board_t + BOARD_TO_DIE_C : NAN;
            own = !s_tps;  // the Supra 400/401 has no diode path: this is its chip reading
        }
        if (isnan(t)) t = 0;
        g_state.chip_temp[i] = t;
        g_state.chip_temp_own[i] = own;
        if (t > tmax) tmax = t;
    }
    g_state.chip_temp_max = tmax;
    g_state.temp_ok = ok;
    g_state.fan_rpm[0] = fan::rpm(0);
    g_state.fan_rpm[1] = 0;

    if (s_tps) {
        float vr = tps546::temperature();
        g_state.vr_temp_ok = !isnan(vr);
        g_state.vr_temp = g_state.vr_temp_ok ? vr : 0;
        if (!s_powered) return;
        float v = tps546::vin();
        if (!isnan(v)) g_state.vin = v;
        float vo = tps546::vout();
        if (!isnan(vo)) g_state.vout = vo;  // a failed read keeps the last value
        g_state.iout = tps546::iout();
        float p = tps546::pout();
        if (!isnan(p)) g_state.pin = p + POWER_OFFSET_W;
        g_state.iin = g_state.vin > 1 ? g_state.pin / g_state.vin : 0;
        return;
    }
    // DS4432U path: no regulator sensor; the board sensor covers it
    g_state.vr_temp_ok = board_ok;
    g_state.vr_temp = board_ok ? board_t : 0;
    float v, i, p;
    if (ds4432u::vin(v)) g_state.vin = v;
    if (!s_powered) return;
    if (ds4432u::iin(i)) g_state.iin = i;
    if (ds4432u::pin(p)) g_state.pin = p;
    g_state.vout = ds4432u::vcore();  // NAN when the ADC can't tell
    g_state.iout = 0;
}

void log_regulator(const char *why)
{
    if (s_tps) tps546::log_status(why);
}

Error check_faults()
{
    if (!s_powered) return Error::None;
    if (!s_tps) {
        // no PMBus status here; a sagging 5 V input is the fault this board can see
        if (g_state.vin > 0.5f && g_state.vin < MIN_VIN_5V) return Error::PsuFault;
        return Error::None;
    }
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
    case Error::NoChips: return AF_NUM_ASICS > 1 ? "No chips answered" : "The chip didn't answer";
    case Error::ChipMissing: return "A chip in a stacked voltage domain didn't answer";
    case Error::PsuFault: return "PSU / input voltage fault";
    case Error::OverCurrent: return "Core over-current";
    case Error::VoutFault: return "Core voltage fault";
    case Error::VrOverTemp: return "Regulator over-temperature";
    }
    return "?";
}

} // namespace board

#endif // AF_BITAXE_FAMILY
