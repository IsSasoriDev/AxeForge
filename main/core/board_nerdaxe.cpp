// NerdAxe: 1x BM1366 on a LilyGo T-Display-S3. Core voltage from a TPS40305
// buck trimmed by a DS4432U current DAC, input power from an INA260, fan and
// board temperature from an EMC2101, core voltage read back on ADC1 ch1.
// The buck enable (GPIO10) is active LOW here, the opposite of the NerdQaxe++.
#include "sdkconfig.h"
#if CONFIG_AF_BOARD_NERDAXE

#include "board.h"

#include <math.h>

#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "asic/bm13xx.h"
#include "core/state.h"
#include "hw/i2c_bus.h"
#include "hw/sensors.h"

static const char *TAG = "board";

#define PIN_PWR_EN GPIO_NUM_10  // LOW = buck on
#define PIN_ASIC_RST ((gpio_num_t) AF_PIN_ASIC_RST)
#define BUCK_ON 0
#define BUCK_OFF 1

#define DS4432U_ADDR 0x48
#define DS4432U_OUT0 0xF8
#define INA260_ADDR 0x40
#define INA260_CURRENT 0x01
#define INA260_VOLTAGE 0x02
#define INA260_POWER 0x03

// DS4432U + TPS40305 transfer function (Bitaxe/NerdAxe resistor values)
#define TPS40305_VFB 0.6f
#define DAC_IFS 0.000098921f  // (Vrfs / Rfs) * 127/16 with Vrfs 0.997 V, Rfs 80 k
#define FB_RA 4750.0f
#define FB_RB 3320.0f
#define VNOM 1.451f  // output with the DAC at 0
#define VMAX 2.39f
#define VMIN 0.046f

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
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static bool s_cali_ok = false;

namespace {
struct Guard {
    Guard() { xSemaphoreTakeRecursive(s_lock, portMAX_DELAY); }
    ~Guard() { xSemaphoreGiveRecursive(s_lock); }
};
}

// Register code for the DS4432U that puts the TPS40305 at `vout`; 0 if out of range.
static uint8_t dac_code(float vout)
{
    if (vout > VMAX || vout < VMIN) return 0;
    float change = fabsf((((TPS40305_VFB / FB_RB) - ((vout - TPS40305_VFB) / FB_RA)) / DAC_IFS) * 127.0f);
    uint8_t reg = (uint8_t) ceilf(change);
    if (vout < VNOM) reg |= 0x80;  // sink current: below the nominal output
    return reg;
}

static bool set_vout(float volts)
{
    uint8_t code = dac_code(volts);
    if (!code) {
        ESP_LOGE(TAG, "refusing core voltage %.3f V", volts);
        return false;
    }
    ESP_LOGI(TAG, "core -> %.3f V (DAC 0x%02x)", volts, code);
    return i2c_bus::write_byte(DS4432U_ADDR, DS4432U_OUT0, code) == ESP_OK;
}

static bool ina_read(uint8_t reg, float scale, bool is_signed, float &out)
{
    uint8_t b[2];
    if (i2c_bus::read(INA260_ADDR, reg, b, 2) != ESP_OK) return false;
    uint16_t raw = (b[0] << 8) | b[1];
    out = (is_signed ? (float) (int16_t) raw : (float) raw) * scale;
    return true;
}

static void adc_init()
{
    adc_oneshot_unit_init_cfg_t u = {};
    u.unit_id = ADC_UNIT_1;
    if (adc_oneshot_new_unit(&u, &s_adc) != ESP_OK) return;
    adc_oneshot_chan_cfg_t c = {};
    c.atten = ADC_ATTEN_DB_12;
    c.bitwidth = ADC_BITWIDTH_DEFAULT;
    adc_oneshot_config_channel(s_adc, ADC_CHANNEL_1, &c);
    adc_cali_curve_fitting_config_t cc = {};
    cc.unit_id = ADC_UNIT_1;
    cc.chan = ADC_CHANNEL_1;
    cc.atten = ADC_ATTEN_DB_12;
    cc.bitwidth = ADC_BITWIDTH_DEFAULT;
    s_cali_ok = adc_cali_create_scheme_curve_fitting(&cc, &s_cali) == ESP_OK;
}

static float vcore_measured()
{
    int raw = 0, mv = 0;
    if (!s_adc || adc_oneshot_read(s_adc, ADC_CHANNEL_1, &raw) != ESP_OK) return 0;
    if (s_cali_ok && adc_cali_raw_to_voltage(s_cali, raw, &mv) == ESP_OK) return mv / 1000.0f;
    return 0;
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
    if (mhz < 400) mhz = 400;
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
    adc_init();
    asic::uart_init();

    uint8_t probe;
    if (i2c_bus::read(DS4432U_ADDR, DS4432U_OUT0, &probe, 1) != ESP_OK) return Error::NoRegulator;
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

    if (!set_vout(mv / 1000.0f)) return Error::NoRegulator;
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
    if (ina_read(INA260_VOLTAGE, 1.25e-3f, false, v)) g_state.vin = v;
    if (!s_powered) return;
    if (ina_read(INA260_CURRENT, 1.25e-3f, true, i)) g_state.iin = i;
    if (ina_read(INA260_POWER, 10e-3f, false, p)) g_state.pin = p;
    g_state.vout = vcore_measured();
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
