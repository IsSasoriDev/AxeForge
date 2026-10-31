#include "tps546.h"

#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "i2c_bus.h"

static const char *TAG = "tps546";

#define ADDR 0x24

// PMBus commands
#define OPERATION 0x01
#define ON_OFF_CONFIG 0x02
#define CLEAR_FAULTS 0x03
#define PHASE 0x04
#define VOUT_MODE 0x20
#define VOUT_COMMAND 0x21
#define VOUT_MAX 0x24
#define VOUT_MARGIN_HIGH 0x25
#define VOUT_MARGIN_LOW 0x26
#define VOUT_SCALE_LOOP 0x29
#define VOUT_MIN 0x2B
#define FREQUENCY_SWITCH 0x33
#define VIN_ON 0x35
#define VIN_OFF 0x36
#define VOUT_OV_FAULT_LIMIT 0x40
#define VOUT_OV_WARN_LIMIT 0x42
#define VOUT_UV_WARN_LIMIT 0x43
#define VOUT_UV_FAULT_LIMIT 0x44
#define IOUT_OC_FAULT_LIMIT 0x46
#define IOUT_OC_FAULT_RESPONSE 0x47
#define IOUT_OC_WARN_LIMIT 0x4A
#define OT_FAULT_LIMIT 0x4F
#define OT_FAULT_RESPONSE 0x50
#define OT_WARN_LIMIT 0x51
#define VIN_OV_FAULT_LIMIT 0x55
#define VIN_OV_FAULT_RESPONSE 0x56
#define VIN_UV_WARN_LIMIT 0x58
#define TON_DELAY 0x60
#define TON_RISE 0x61
#define TON_MAX_FAULT_LIMIT 0x62
#define TON_MAX_FAULT_RESPONSE 0x63
#define TOFF_DELAY 0x64
#define TOFF_FALL 0x65
#define STATUS_BYTE 0x78
#define STATUS_VOUT 0x7A
#define STATUS_IOUT 0x7B
#define STATUS_INPUT 0x7C
#define STATUS_TEMPERATURE 0x7D
#define READ_VIN 0x88
#define READ_VOUT 0x8B
#define READ_IOUT 0x8C
#define READ_TEMPERATURE_1 0x8D
#define IC_DEVICE_ID 0xAD
#define COMPENSATION_CONFIG 0xB1
#define SYNC_CONFIG 0xE4
#define STACK_CONFIG 0xEC
#define PIN_DETECT_OVERRIDE 0xEE

#define OP_OFF 0x00
#define OP_ON 0x80

// Limits relative to VOUT_COMMAND, as in the reference profiles
#define OV_FAULT_RATIO 1.25f
#define MARGIN_HIGH_RATIO 1.10f
#define MARGIN_LOW_RATIO 0.90f
#define UV_WARN_RATIO 0.90f
#define UV_FAULT_RATIO 0.75f

static const uint8_t DEVICE_IDS[][6] = {
    {0x54, 0x49, 0x54, 0x6B, 0x24, 0x41},  // TPS546D24A
    {0x54, 0x49, 0x54, 0x6D, 0x24, 0x41},  // TPS546D24A
    {0x54, 0x49, 0x54, 0x6D, 0x24, 0x62},  // TPS546D24S
};

// ON_OFF_CONFIG as AxeOS writes it for every Bitaxe: the OPERATION command alone
// switches the output; the CONTROL pin is ignored (bit 2 clear). With the pin
// obeyed (0x1F) a GT dropped its core seconds after power-up and stayed off.
// The Supra Hex keeps 0x1F: tested that way.
#define ON_OFF_CMD_ONLY 0x1B

namespace tps546 {
const Profile NERDQAXE_REV7 = {"rev7 stack", 2, 11.0f, 10.5f, 11.0f, 14.0f, 0.125f, 1.0f, 1.5f,
                               90.0f, 100.0f, 1.10f, 0x18, 0x0001, 0x0000, {0x12, 0x34, 0x42, 0x25, 0x04}};
// from the Bitaxe reference firmware (VIN 11.5/11.0 V, 2.5..4.5 V out, 25/30 A)
const Profile BITAXE_HEX = {"hex", 3, 11.5f, 11.0f, 11.0f, 14.0f, 0.125f, 2.5f / 3, 4.5f / 3,
                            25.0f, 30.0f, 1.16f, 0x1F, 0, 0xFFFF, {0}};
// reference TPS546_CONFIG_DEFAULT: VIN 4.8/4.5 V (no UV warning), 1..2 V out, 25/30 A
const Profile BITAXE_SINGLE = {"single", 1, 4.8f, 4.5f, 0.0f, 6.5f, 0.25f, 1.0f, 2.0f,
                               25.0f, 30.0f, 1.16f, ON_OFF_CMD_ONLY, 0, 0xFFFF, {0}};
// reference TPS546_CONFIG_NAJA_DUO: VIN 11/10.5 V, 1.8..3.0 V out, 61/68 A, two-phase stack
const Profile NAJA_DUO = {"naja duo", 2, 11.0f, 10.5f, 11.0f, 14.0f, 0.125f, 1.8f / 2, 3.0f / 2,
                          61.0f, 68.0f, 1.10f, ON_OFF_CMD_ONLY, 0x0001, 0xFFFF, {0x12, 0x34, 0x42, 0x25, 0x04}};
// reference TPS546_CONFIG_GAMMA_TURBO: VIN 11/10.5 V, 50/55 A, two-phase stack. The
// reference allows 1..3 V out; one domain of BM1370s never needs more than 1.5 V
const Profile GAMMA_TURBO = {"gamma turbo", 1, 11.0f, 10.5f, 11.0f, 14.8f, 0.25f, 1.0f, 1.5f,
                             50.0f, 55.0f, 1.10f, ON_OFF_CMD_ONLY, 0x0001, 0xFFFF, {0x12, 0x34, 0x42, 0x21, 0x04}};
// reference TPS546_CONFIG_GAMMA_HEX: VIN 11/10.5 V, 2.0..3.0 V out, 150/160 A, four-phase stack
const Profile GAMMA_HEX = {"gamma hex", 2, 11.0f, 10.5f, 11.0f, 14.0f, 0.125f, 2.0f / 2, 3.0f / 2,
                           150.0f, 160.0f, 1.10f, ON_OFF_CMD_ONLY, 0x0003, 0xFFFF, {0x02, 0x64, 0x82, 0x11, 0x06}};
}

static const tps546::Profile *s_prof = &tps546::NERDQAXE_REV7;

static bool s_initialized = false;
static int s_vout_exp = -9;      // ULINEAR16 exponent from VOUT_MODE
static bool s_relative = false;  // VOUT_MODE REL: limits are ratios of VOUT_COMMAND
static float s_vout_cmd = 0;

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

static float ulinear16_to_float(uint16_t v) { return v * powf(2.0f, (float) s_vout_exp); }
static uint16_t float_to_ulinear16(float x) { return (uint16_t) lroundf(x / powf(2.0f, (float) s_vout_exp)); }

static bool w8(uint8_t cmd, uint8_t v) { return i2c_bus::write_byte(ADDR, cmd, v) == ESP_OK; }
static bool w16(uint8_t cmd, uint16_t v) { return i2c_bus::write_word_le(ADDR, cmd, v) == ESP_OK; }
static bool wlin(uint8_t cmd, float v) { return w16(cmd, float_to_slinear11(v)); }
static bool wvout(uint8_t cmd, float v) { return w16(cmd, float_to_ulinear16(v)); }

// SMBus block read: the first byte on the wire is the length
static bool read_block(uint8_t cmd, uint8_t *data, size_t len)
{
    uint8_t buf[34];
    if (len + 1 > sizeof(buf)) return false;
    if (i2c_bus::read(ADDR, cmd, buf, len + 1) != ESP_OK) return false;
    memcpy(data, buf + 1, len);
    return true;
}

static bool write_block(uint8_t cmd, const uint8_t *data, size_t len)
{
    uint8_t buf[34];
    if (len + 2 > sizeof(buf)) return false;
    buf[0] = cmd;
    buf[1] = (uint8_t) len;
    memcpy(buf + 2, data, len);
    return i2c_bus::write(ADDR, buf, len + 2) == ESP_OK;
}

static float read_lin(uint8_t cmd)
{
    uint16_t v = 0;
    if (i2c_bus::read_word_le(ADDR, cmd, &v) != ESP_OK) return NAN;
    return slinear11_to_float(v);
}

static uint8_t read_status(uint8_t cmd)
{
    uint8_t v = 0xff;
    if (!s_initialized) return 0xff;
    if (i2c_bus::read(ADDR, cmd, &v, 1) != ESP_OK) return 0xff;
    return v;
}

// In relative mode the chip scales the limits itself; in absolute mode they
// have to follow VOUT_COMMAND.
static float lim(float ratio, float vout) { return s_relative ? ratio : ratio * vout; }

static bool write_upper_limits(float vout)
{
    bool ok = wvout(VOUT_OV_FAULT_LIMIT, lim(OV_FAULT_RATIO, vout));
    ok &= wvout(VOUT_OV_WARN_LIMIT, lim(s_prof->ov_warn_ratio, vout));
    ok &= wvout(VOUT_MARGIN_HIGH, lim(MARGIN_HIGH_RATIO, vout));
    return ok;
}

static bool write_lower_limits(float vout)
{
    bool ok = wvout(VOUT_MARGIN_LOW, lim(MARGIN_LOW_RATIO, vout));
    ok &= wvout(VOUT_UV_WARN_LIMIT, lim(UV_WARN_RATIO, vout));
    ok &= wvout(VOUT_UV_FAULT_LIMIT, lim(UV_FAULT_RATIO, vout));
    return ok;
}

namespace tps546 {

bool detect()
{
    uint8_t id[6];
    if (!read_block(IC_DEVICE_ID, id, sizeof(id))) return false;
    for (const auto &known : DEVICE_IDS)
        if (memcmp(id, known, sizeof(id)) == 0) return true;
    ESP_LOGW(TAG, "unknown device id %02x %02x %02x %02x %02x %02x", id[0], id[1], id[2], id[3], id[4], id[5]);
    return false;
}

bool init(const Profile &profile)
{
    s_initialized = false;
    s_prof = &profile;
    const int domains = profile.domains;
    if (!detect()) {
        ESP_LOGE(TAG, "TPS546 not found");
        return false;
    }
    uint8_t mode = 0;
    if (i2c_bus::read(ADDR, VOUT_MODE, &mode, 1) != ESP_OK) return false;
    s_vout_exp = (mode & 0x10) ? (int) (mode & 0x1f) - 32 : (mode & 0x1f);
    s_relative = (mode & 0x80) != 0;

    const float vout = 1.2f * domains;
    bool ok = w8(OPERATION, OP_OFF);
    ok &= w8(ON_OFF_CONFIG, profile.on_off_config);
    if (profile.stack_config) {
        ok &= w16(STACK_CONFIG, profile.stack_config);
        ok &= w8(SYNC_CONFIG, 0xD0);
    }
    ok &= w8(PHASE, 0xFF);
    ok &= wlin(FREQUENCY_SWITCH, 650);  // kHz
    if (profile.compensation[0]) ok &= write_block(COMPENSATION_CONFIG, profile.compensation, sizeof(profile.compensation));

    ok &= wlin(VIN_UV_WARN_LIMIT, profile.vin_uv_warn);
    ok &= wlin(VIN_ON, profile.vin_on);
    ok &= wlin(VIN_OFF, profile.vin_off);
    ok &= wlin(VIN_OV_FAULT_LIMIT, profile.vin_ov_fault);
    ok &= w8(VIN_OV_FAULT_RESPONSE, 0xB7);

    ok &= wlin(VOUT_SCALE_LOOP, profile.scale_loop);
    ok &= wvout(VOUT_COMMAND, vout);
    ok &= wvout(VOUT_MAX, profile.core_max * domains);
    ok &= wvout(VOUT_MIN, profile.core_min * domains);
    ok &= write_upper_limits(vout);
    ok &= write_lower_limits(vout);

    ok &= wlin(IOUT_OC_WARN_LIMIT, profile.oc_warn_a);
    ok &= wlin(IOUT_OC_FAULT_LIMIT, profile.oc_fault_a);
    ok &= w8(IOUT_OC_FAULT_RESPONSE, 0xC0);  // shut down, no retry

    ok &= wlin(OT_WARN_LIMIT, 105.0f);
    ok &= wlin(OT_FAULT_LIMIT, 145.0f);
    ok &= w8(OT_FAULT_RESPONSE, 0xFF);

    ok &= wlin(TON_DELAY, 0);
    ok &= wlin(TON_RISE, 3);
    ok &= wlin(TON_MAX_FAULT_LIMIT, 0);
    ok &= w8(TON_MAX_FAULT_RESPONSE, 0x3B);
    ok &= wlin(TOFF_DELAY, 0);
    ok &= wlin(TOFF_FALL, 0);
    ok &= w16(PIN_DETECT_OVERRIDE, profile.pin_detect_override);
    // Never STORE_USER_ALL: a bad NVM write can leave the part unrecoverable.
    // The config above is rewritten on every power-up instead.
    if (!ok) {
        ESP_LOGE(TAG, "config write failed");
        w8(OPERATION, OP_OFF);
        return false;
    }
    s_vout_cmd = vout;
    clear_faults();
    s_initialized = true;
    ESP_LOGI(TAG, "initialized (%s, %d domains): VOUT_MODE %02x (%s limits)", profile.name, domains, mode,
             s_relative ? "relative" : "absolute");
    return true;
}

bool set_core(float volts)
{
    if (!s_initialized) return false;
    if (volts < s_prof->core_min || volts > s_prof->core_max) {
        ESP_LOGE(TAG, "refusing out-of-range core voltage %.3f", volts);
        return false;
    }
    float v = volts * s_prof->domains;
    // keep the output inside its OV/UV window while it moves
    bool ok;
    if (v >= s_vout_cmd) ok = write_upper_limits(v) && wvout(VOUT_COMMAND, v) && write_lower_limits(v);
    else ok = write_lower_limits(v) && wvout(VOUT_COMMAND, v) && write_upper_limits(v);
    if (!ok) {
        ESP_LOGE(TAG, "could not set vout %.3f", v);
        return false;
    }
    s_vout_cmd = v;
    clear_faults();
    ESP_LOGI(TAG, "core -> %.3fV (vout %.3fV)", volts, v);
    return w8(OPERATION, OP_ON);
}

bool off() { return w8(OPERATION, OP_OFF); }

void clear_faults() { i2c_bus::send_cmd(ADDR, CLEAR_FAULTS); }

float temperature() { return read_lin(READ_TEMPERATURE_1); }

float vin() { return s_initialized ? read_lin(READ_VIN) : 0.0f; }
float iout() { return s_initialized ? read_lin(READ_IOUT) : 0.0f; }

float vout()
{
    if (!s_initialized) return 0.0f;
    uint16_t v = 0;
    if (i2c_bus::read_word_le(ADDR, READ_VOUT, &v) != ESP_OK) return NAN;  // unknown, not 0 V
    return ulinear16_to_float(v) / s_prof->domains;
}

float pout() { return vout() * s_prof->domains * iout(); }

// Why the output went away: every status register in one line, for the log.
void log_status(const char *why)
{
    uint16_t word = 0xffff;
    if (s_initialized) i2c_bus::read_word_le(ADDR, 0x79, &word);  // STATUS_WORD
    ESP_LOGE(TAG, "%s: STATUS_WORD %04x VOUT %02x IOUT %02x INPUT %02x TEMP %02x CML %02x | vout %.3f V, iout %.1f A, vin %.2f V",
             why, word, read_status(STATUS_VOUT), read_status(STATUS_IOUT), read_status(STATUS_INPUT),
             read_status(STATUS_TEMPERATURE), read_status(0x7E), vout(), iout(), vin());
}

uint8_t status_byte() { return read_status(STATUS_BYTE); }
uint8_t status_iout() { return read_status(STATUS_IOUT); }
uint8_t status_vout() { return read_status(STATUS_VOUT); }
uint8_t status_input() { return read_status(STATUS_INPUT); }
uint8_t status_temp() { return read_status(STATUS_TEMPERATURE); }

} // namespace tps546
