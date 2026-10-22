#include "tps53647.h"

#include <math.h>
#include "esp_log.h"
#include "i2c_bus.h"

static const char *TAG = "tps53647";

#define ADDR 0x71
#define DEVICE_CODE 0x01f0
#define HW_MIN_VOLTAGE 0.25f  // VID step 5 mV from 0.25 V

// PMBus commands
#define ON_OFF_CONFIG 0x02
#define CLEAR_FAULTS 0x03
#define RESTORE_DEFAULT_ALL 0x12
#define VOUT_COMMAND 0x21
#define IOUT_OC_FAULT_LIMIT 0x46
#define IOUT_OC_WARN_LIMIT 0x4A
#define OT_FAULT_LIMIT 0x4F
#define OT_WARN_LIMIT 0x51
#define STATUS_BYTE 0x78
#define STATUS_VOUT 0x7A
#define STATUS_IOUT 0x7B
#define STATUS_INPUT 0x7C
#define STATUS_TEMPERATURE 0x7D
#define READ_VIN 0x88
#define READ_IIN 0x89
#define READ_IOUT 0x8C
#define READ_TEMPERATURE_1 0x8D
#define READ_PIN 0x97
#define MFR_04_VOUT 0xD4
#define MFR_10_IMAX 0xDA
#define MFR_12 0xDC
#define MFR_13 0xDD
#define MFR_20_PHASES 0xE4
#define MFR_44_DEVCODE 0xFC

static bool s_initialized = false;

static float slinear11_to_float(uint16_t value)
{
    int32_t exponent = value >> 11;
    int32_t mantissa = value & 0x7ff;
    if (exponent & 0x10) exponent |= 0xffffffe0;
    if (mantissa & 0x400) mantissa |= 0xfffff800;
    return mantissa * powf(2.0f, (float) exponent);
}

static uint16_t float_to_slinear11(float x)
{
    if (x <= 0.0f) return 0;
    int32_t e = -16, m = 0;
    for (; e <= 15; e++) {
        m = (int32_t) roundf(x / powf(2.0f, (float) e));
        if (m >= 0 && m <= 1023) break;
    }
    if (e > 15) return 0;
    return (uint16_t) (((e & 0x1f) << 11) | (m & 0x7ff));
}

static float read_linear(uint8_t reg)
{
    if (!s_initialized) return 0.0f;
    uint16_t v = 0;
    if (i2c_bus::read_word_le(ADDR, reg, &v) != ESP_OK) return 0.0f;
    return slinear11_to_float(v);
}

static uint8_t read_status(uint8_t reg)
{
    uint8_t v = 0xff;
    if (i2c_bus::read(ADDR, reg, &v, 1) != ESP_OK) return 0xff;
    return v;
}

namespace tps53647 {

bool detect()
{
    uint16_t code = 0;
    if (i2c_bus::read_word_le(ADDR, MFR_44_DEVCODE, &code) != ESP_OK) return false;
    ESP_LOGI(TAG, "device code %04x", code);
    return code == DEVICE_CODE;
}

bool init(int phases, int imax_a, float ifault_a)
{
    if (!detect()) {
        ESP_LOGE(TAG, "TPS53647 not found");
        return false;
    }
    const uint8_t on_off = 0b00010111;
    i2c_bus::send_cmd(ADDR, CLEAR_FAULTS);
    i2c_bus::send_cmd(ADDR, RESTORE_DEFAULT_ALL);
    i2c_bus::write_byte(ADDR, ON_OFF_CONFIG, on_off);
    i2c_bus::write_byte(ADDR, MFR_12, 0x20);
    i2c_bus::write_byte(ADDR, MFR_10_IMAX, (uint8_t) imax_a);
    i2c_bus::write_byte(ADDR, MFR_13, 0x89);
    i2c_bus::write_byte(ADDR, ON_OFF_CONFIG, on_off);
    i2c_bus::write_byte(ADDR, MFR_12, 0x20);
    if (phases >= 1 && phases <= 6) {
        i2c_bus::write_byte(ADDR, MFR_20_PHASES, (uint8_t) (phases - 1));
    }
    i2c_bus::write_word_le(ADDR, OT_WARN_LIMIT, float_to_slinear11(95.0f));
    i2c_bus::write_word_le(ADDR, OT_FAULT_LIMIT, float_to_slinear11(125.0f));
    i2c_bus::write_word_le(ADDR, IOUT_OC_WARN_LIMIT, float_to_slinear11(ifault_a));
    i2c_bus::write_word_le(ADDR, IOUT_OC_FAULT_LIMIT, float_to_slinear11(ifault_a));
    s_initialized = true;
    ESP_LOGI(TAG, "initialized: %d phases, imax %dA", phases, imax_a);
    return true;
}

bool set_vout(float volts)
{
    if (volts == 0.0f) return true;
    if (volts < VOUT_MIN || volts > VOUT_MAX) {
        ESP_LOGE(TAG, "refusing out-of-range vout %.3f", volts);
        return false;
    }
    int vid = (int) ((volts - HW_MIN_VOLTAGE) / 0.005f) + 1;
    if (vid > 0xff) return false;
    if (vid == 0x97) vid = 0x96;  // matches reference behaviour
    esp_err_t err = i2c_bus::write_word_le(ADDR, VOUT_COMMAND, (uint16_t) vid);
    ESP_LOGI(TAG, "vout -> %.3fV (vid 0x%02x)", volts, vid);
    return err == ESP_OK;
}

void clear_faults() { i2c_bus::send_cmd(ADDR, CLEAR_FAULTS); }

float temperature() { return read_linear(READ_TEMPERATURE_1); }
float vin() { return read_linear(READ_VIN); }
float iin() { return read_linear(READ_IIN); }
float pin() { return read_linear(READ_PIN); }
float iout() { return read_linear(READ_IOUT); }

float vout()
{
    if (!s_initialized) return 0.0f;
    uint16_t v = 0;
    if (i2c_bus::read_word_le(ADDR, MFR_04_VOUT, &v) != ESP_OK) return 0.0f;
    return (float) v / 512.0f;  // ULINEAR16, exponent -9
}

uint8_t status_byte() { return read_status(STATUS_BYTE); }
uint8_t status_iout() { return read_status(STATUS_IOUT); }
uint8_t status_vout() { return read_status(STATUS_VOUT); }
uint8_t status_input() { return read_status(STATUS_INPUT); }
uint8_t status_temp() { return read_status(STATUS_TEMPERATURE); }

} // namespace tps53647
