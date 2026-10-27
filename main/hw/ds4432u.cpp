#include "ds4432u.h"

#include <math.h>
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "i2c_bus.h"

static const char *TAG = "ds4432u";

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

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static bool s_cali_ok = false;

// Register code for the DS4432U that puts the TPS40305 at `vout`; 0 if out of range.
static uint8_t dac_code(float vout)
{
    if (vout > VMAX || vout < VMIN) return 0;
    float change = fabsf((((TPS40305_VFB / FB_RB) - ((vout - TPS40305_VFB) / FB_RA)) / DAC_IFS) * 127.0f);
    uint8_t reg = (uint8_t) ceilf(change);
    if (vout < VNOM) reg |= 0x80;  // sink current: below the nominal output
    return reg;
}

static bool ina_read(uint8_t reg, float scale, bool is_signed, float &out)
{
    uint8_t b[2];
    if (i2c_bus::read(INA260_ADDR, reg, b, 2) != ESP_OK) return false;
    uint16_t raw = (b[0] << 8) | b[1];
    out = (is_signed ? (float) (int16_t) raw : (float) raw) * scale;
    return true;
}

namespace ds4432u {

bool present()
{
    uint8_t probe;
    return i2c_bus::read(DS4432U_ADDR, DS4432U_OUT0, &probe, 1) == ESP_OK;
}

void init()
{
    if (s_adc) return;
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
    if (!s_cali_ok) ESP_LOGW(TAG, "no ADC calibration: core voltage readback unavailable");
}

bool set_vout(float volts)
{
    uint8_t code = dac_code(volts);
    if (!code) {
        ESP_LOGE(TAG, "refusing core voltage %.3f V", volts);
        return false;
    }
    ESP_LOGI(TAG, "core -> %.3f V (DAC 0x%02x)", volts, code);
    return i2c_bus::write_byte(DS4432U_ADDR, DS4432U_OUT0, code) == ESP_OK;
}

bool vin(float &v) { return ina_read(INA260_VOLTAGE, 1.25e-3f, false, v); }
bool iin(float &a) { return ina_read(INA260_CURRENT, 1.25e-3f, true, a); }
bool pin(float &w) { return ina_read(INA260_POWER, 10e-3f, false, w); }

float vcore()
{
    int raw = 0, mv = 0;
    if (!s_adc || adc_oneshot_read(s_adc, ADC_CHANNEL_1, &raw) != ESP_OK) return NAN;
    // without calibration the raw count can't be turned into volts: say so, not 0 V
    if (s_cali_ok && adc_cali_raw_to_voltage(s_cali, raw, &mv) == ESP_OK) return mv / 1000.0f;
    return NAN;
}

} // namespace ds4432u
