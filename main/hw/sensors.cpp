#include "sensors.h"

#include <stdio.h>
#include <string.h>

#include <math.h>
#include "esp_log.h"
#include "i2c_bus.h"
#include "core/board_config.h"

static const char *TAG = "sensors";

#if CONFIG_AF_BOARD_NERDAXE || AF_EMC2101
// ---------------------------------------------------------------- EMC2101 (NerdAxe, Supra, Ultra, Gamma, Gamma Duo)
// One fan, the board temperature and the chip's diode. A DS4432U can sit at 0x48
// on these boards, so the TMP1075 scan used elsewhere must never run here.
#define EMC2101_ADDR 0x4C
#define EMC2101_INTERNAL_TEMP 0x00
#define EMC2101_EXTERNAL_MSB 0x01
#define EMC2101_EXTERNAL_LSB 0x10
#define EMC2101_IDEALITY 0x17
#define EMC2101_BETA 0x18
#define EMC2101_DIODE_OPEN 0x3F8   // 11-bit readings the part uses for a missing or
#define EMC2101_DIODE_SHORT 0x3FF  // shorted diode
#define EMC2101_CONFIG 0x03
#define EMC2101_TACH_LSB 0x46
#define EMC2101_TACH_MSB 0x47
#define EMC2101_FAN_CONFIG 0x4A
#define EMC2101_FAN_SETTING 0x4C  // 0..63
#define EMC2101_CONFIG_VALUE 0x04  // enable the tach input
#define EMC2101_FAN_CONFIG_VALUE 0x23

static bool s_emc_ok = false;

namespace fan {

bool init()
{
    s_emc_ok = i2c_bus::write_byte(EMC2101_ADDR, EMC2101_CONFIG, EMC2101_CONFIG_VALUE) == ESP_OK &&
               i2c_bus::write_byte(EMC2101_ADDR, EMC2101_FAN_CONFIG, EMC2101_FAN_CONFIG_VALUE) == ESP_OK;
#ifdef AF_EMC_IDEALITY
    // diode model of the chip (reference firmware values)
    s_emc_ok &= i2c_bus::write_byte(EMC2101_ADDR, EMC2101_IDEALITY, AF_EMC_IDEALITY) == ESP_OK;
    s_emc_ok &= i2c_bus::write_byte(EMC2101_ADDR, EMC2101_BETA, AF_EMC_BETA) == ESP_OK;
#endif
    if (!s_emc_ok) ESP_LOGE(TAG, "EMC2101 init failed");
    return s_emc_ok;
}

void set_pct(int channel, float pct)
{
    if (channel != 0) return;  // single fan header
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    i2c_bus::write_byte(EMC2101_ADDR, EMC2101_FAN_SETTING, (uint8_t) (pct * 63.0f / 100.0f + 0.5f));
}

uint16_t rpm(int channel)
{
    if (channel != 0) return 0;
    uint8_t lsb = 0, msb = 0;
    if (i2c_bus::read(EMC2101_ADDR, EMC2101_TACH_LSB, &lsb, 1) != ESP_OK) return 0;
    if (i2c_bus::read(EMC2101_ADDR, EMC2101_TACH_MSB, &msb, 1) != ESP_OK) return 0;
    uint16_t raw = lsb | (msb << 8);
    if (raw == 0 || raw == 0xffff) return 0;
    uint32_t r = 5400000u / raw;
    return r > 65535 ? 0 : (uint16_t) r;
}

bool check()
{
    uint8_t cfg = 0;
    if (i2c_bus::read(EMC2101_ADDR, EMC2101_FAN_CONFIG, &cfg, 1) != ESP_OK) return true;
    if (cfg == EMC2101_FAN_CONFIG_VALUE) return true;
    ESP_LOGW(TAG, "fan controller config is %02x, it was reset - re-initialising", cfg);
    init();
    return false;
}

} // namespace fan

namespace temp {

int detect()
{
    uint8_t t;
    bool ok = false;
    for (int tries = 0; tries < 3 && !ok; tries++)
        ok = i2c_bus::read(EMC2101_ADDR, EMC2101_INTERNAL_TEMP, &t, 1) == ESP_OK;
    ESP_LOGI(TAG, "%d ASIC temperature sensors (EMC2101)", ok ? 1 : 0);
    return ok ? 1 : 0;
}

int count() { return s_emc_ok ? 1 : detect(); }

float asic_area(int index)
{
    if (index != 0) return NAN;
    uint8_t t;
    if (i2c_bus::read(EMC2101_ADDR, EMC2101_INTERNAL_TEMP, &t, 1) != ESP_OK) return NAN;
    if (t == 0 || t > 127) return NAN;  // 0 C on a running miner, or a negative reading: garbage
    return t;
}

float vr_board() { return NAN; }

float chip_diode(int index)
{
    if (index != 0) return NAN;
    uint8_t msb = 0, lsb = 0;
    if (i2c_bus::read(EMC2101_ADDR, EMC2101_EXTERNAL_MSB, &msb, 1) != ESP_OK) return NAN;
    if (i2c_bus::read(EMC2101_ADDR, EMC2101_EXTERNAL_LSB, &lsb, 1) != ESP_OK) return NAN;
    int16_t raw = (int16_t) (((msb << 8) | lsb) >> 5);  // 11-bit, 1/8 C
    if (raw == EMC2101_DIODE_OPEN || raw == EMC2101_DIODE_SHORT) return NAN;
    if (raw & 0x400) raw |= (int16_t) 0xF800;
    float t = raw / 8.0f;
    return t > -10 && t < 150 ? t : NAN;
}

} // namespace temp

#elif AF_EMC2103
// ---------------------------------------------------------------- EMC2103 (Naja Duo, GT, Gamma Hex)
// One fan, the board temperature and two chip diodes.
#define EMC2103_ADDR 0x2E
#define EMC2103_INTERNAL_MSB 0x00
#define EMC2103_EXT1_MSB 0x02
#define EMC2103_EXT1_LSB 0x03
#define EMC2103_EXT2_MSB 0x04
#define EMC2103_EXT2_LSB 0x05
#define EMC2103_DIODE_FAULT 0x8000
#define EMC2103_IDEALITY1 0x11
#define EMC2103_IDEALITY2 0x12
#define EMC2103_BETA1 0x14
#define EMC2103_BETA2 0x15
#define EMC2103_CONFIG1 0x20
#define EMC2103_PWM_CONFIG 0x2A
#define EMC2103_PWM_BASE_FREQ 0x2B
#define EMC2103_FAN_SETTING 0x40  // 0..255
#define EMC2103_PWM_DIVIDE 0x41
#define EMC2103_FAN_CONFIG1 0x42
#define EMC2103_LUT_CONFIG1 0x50
#define EMC2103_FAN_CONFIG1_DIRECT 0x2B  // reference firmware's direct-PWM setup (Gamma Hex)
#define EMC2103_TACH_MSB 0x4E
#define EMC2103_TACH_LSB 0x4F
#if AF_ASIC_BM1373
// The BM1373 diodes read high and stretched; the second one sits warmer still
// (reference firmware calibration for the Naja Duo).
static const float DIODE_SCALE = 0.9613546f;
static const float DIODE_OFFSET[2] = {-5.0858026f, -5.0858026f - 10.7f};
#else
static const float DIODE_SCALE = 1.0f;
static const float DIODE_OFFSET[2] = {0.0f, 0.0f};
#endif

static bool s_emc_ok = false;

namespace fan {

bool init()
{
    s_emc_ok = i2c_bus::write_byte(EMC2103_ADDR, EMC2103_CONFIG1, 0x00) == ESP_OK;
#if AF_EMC_DIRECT_PWM
    // the fan setting drives the PWM duty directly, push-pull at 26 kHz
    s_emc_ok &= i2c_bus::write_byte(EMC2103_ADDR, EMC2103_LUT_CONFIG1, 0x00) == ESP_OK &&
                i2c_bus::write_byte(EMC2103_ADDR, EMC2103_FAN_CONFIG1, EMC2103_FAN_CONFIG1_DIRECT) == ESP_OK &&
                i2c_bus::write_byte(EMC2103_ADDR, EMC2103_PWM_CONFIG, 0x10) == ESP_OK &&
                i2c_bus::write_byte(EMC2103_ADDR, EMC2103_PWM_BASE_FREQ, 0x00) == ESP_OK &&
                i2c_bus::write_byte(EMC2103_ADDR, EMC2103_PWM_DIVIDE, 0x01) == ESP_OK;
#else
    s_emc_ok &= i2c_bus::write_byte(EMC2103_ADDR, EMC2103_PWM_CONFIG, 0x00) == ESP_OK;
#endif
#ifdef AF_EMC_IDEALITY
    s_emc_ok &= i2c_bus::write_byte(EMC2103_ADDR, EMC2103_IDEALITY1, AF_EMC_IDEALITY) == ESP_OK &&
                i2c_bus::write_byte(EMC2103_ADDR, EMC2103_IDEALITY2, AF_EMC_IDEALITY) == ESP_OK &&
                i2c_bus::write_byte(EMC2103_ADDR, EMC2103_BETA1, AF_EMC_BETA) == ESP_OK &&
                i2c_bus::write_byte(EMC2103_ADDR, EMC2103_BETA2, AF_EMC_BETA) == ESP_OK;
#endif
    if (!s_emc_ok) ESP_LOGE(TAG, "EMC2103 init failed");
    return s_emc_ok;
}

void set_pct(int channel, float pct)
{
    if (channel != 0) return;  // single fan header
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    i2c_bus::write_byte(EMC2103_ADDR, EMC2103_FAN_SETTING, (uint8_t) (pct * 2.55f + 0.5f));
}

uint16_t rpm(int channel)
{
    if (channel != 0) return 0;
    uint8_t lsb = 0, msb = 0;
    if (i2c_bus::read(EMC2103_ADDR, EMC2103_TACH_LSB, &lsb, 1) != ESP_OK) return 0;
    if (i2c_bus::read(EMC2103_ADDR, EMC2103_TACH_MSB, &msb, 1) != ESP_OK) return 0;
    uint16_t raw = (uint16_t) ((lsb | (msb << 8)) >> 3);
    if (raw == 0 || raw >= 0x1fff) return 0;
    uint32_t r = 7864320u / raw;  // 2 edges per rev, default multiplier
    return r > 65535 || r == 82 ? 0 : (uint16_t) r;  // 82 rpm: the stalled-tach reading
}

bool check()
{
    // a register this firmware set away from its reset value tells whether the
    // controller lost its setup
#if defined(AF_EMC_IDEALITY)
    const uint8_t reg = EMC2103_IDEALITY1, want = AF_EMC_IDEALITY;
#elif AF_EMC_DIRECT_PWM
    const uint8_t reg = EMC2103_FAN_CONFIG1, want = EMC2103_FAN_CONFIG1_DIRECT;
#endif
#if defined(AF_EMC_IDEALITY) || AF_EMC_DIRECT_PWM
    uint8_t v = 0;
    if (i2c_bus::read(EMC2103_ADDR, reg, &v, 1) != ESP_OK) return true;
    if (v == want) return true;
    ESP_LOGW(TAG, "fan controller config is %02x, it was reset - re-initialising", v);
    init();
    return false;
#else
    return true;  // nothing this firmware sets differs from the reset state
#endif
}

} // namespace fan

static float diode(int i, uint8_t msb_reg, uint8_t lsb_reg)
{
    uint8_t msb = 0, lsb = 0;
    if (i2c_bus::read(EMC2103_ADDR, msb_reg, &msb, 1) != ESP_OK) return NAN;
    if (i2c_bus::read(EMC2103_ADDR, lsb_reg, &lsb, 1) != ESP_OK) return NAN;
    uint16_t word = (msb << 8) | lsb;
    if (word == EMC2103_DIODE_FAULT) return NAN;
    int16_t raw = (int16_t) (word >> 5);
    if (raw & 0x400) raw |= (int16_t) 0xF800;
    float t = raw / 8.0f * DIODE_SCALE + DIODE_OFFSET[i];
    return t > -10 && t < 150 ? t : NAN;
}

namespace temp {

int detect()
{
    uint8_t t;
    bool ok = false;
    for (int tries = 0; tries < 3 && !ok; tries++)
        ok = i2c_bus::read(EMC2103_ADDR, EMC2103_INTERNAL_MSB, &t, 1) == ESP_OK;
    ESP_LOGI(TAG, "%d board temperature sensors (EMC2103)", ok ? 1 : 0);
    return ok ? 1 : 0;
}

int count() { return s_emc_ok ? 1 : detect(); }

float asic_area(int index)
{
    if (index != 0) return NAN;
    uint8_t t;
    if (i2c_bus::read(EMC2103_ADDR, EMC2103_INTERNAL_MSB, &t, 1) != ESP_OK) return NAN;
    if (t == 0 || t > 127) return NAN;
    return t;
}

float vr_board() { return NAN; }

float chip_diode(int index)
{
#if AF_EMC_FLIP
    index = index == 0 ? 1 : index == 1 ? 0 : index;  // diode 2 sits on the first chip
#endif
    if (index == 0) return diode(0, EMC2103_EXT1_MSB, EMC2103_EXT1_LSB);
    if (index == 1) return diode(1, EMC2103_EXT2_MSB, EMC2103_EXT2_LSB);
    return NAN;
}

} // namespace temp

#else  // NerdQaxe family / Supra Hex

// ---------------------------------------------------------------- EMC2302
#if AF_HEX_FAMILY
#define EMC_ADDR 0x2f  // EMC2302-2 variant
#else
#define EMC_ADDR 0x2e
#endif
#define EMC_POLARITY 0x2a
#define EMC_OUTPUT_CONFIG 0x2b
#define EMC_BASE_F123 0x2d
#define EMC_FAN1 0x30
#define EMC_FAN2 0x40
#define EMC_OFS_SETTING 0x00
#define EMC_OFS_CONFIG1 0x02
#define EMC_OFS_TACH_MSB 0x0e
#define EMC_OFS_TACH_LSB 0x0f

// channel 0 = ASIC fan (connector M2 -> EMC fan 2), channel 1 = VR fan
static uint8_t base_for(int channel) { return channel == 0 ? EMC_FAN2 : EMC_FAN1; }

namespace fan {

bool init()
{
    bool ok = i2c_bus::write_byte(EMC_ADDR, EMC_POLARITY, 0x00) == ESP_OK;
    ok &= i2c_bus::write_byte(EMC_ADDR, EMC_OUTPUT_CONFIG, 0x03) == ESP_OK;
    ok &= i2c_bus::write_byte(EMC_ADDR, EMC_BASE_F123, (1 << 0) | (1 << 3)) == ESP_OK;
    ok &= i2c_bus::write_byte(EMC_ADDR, EMC_FAN1 + EMC_OFS_CONFIG1, (0b01 << 3)) == ESP_OK;
    ok &= i2c_bus::write_byte(EMC_ADDR, EMC_FAN2 + EMC_OFS_CONFIG1, (0b01 << 3)) == ESP_OK;
    if (!ok) ESP_LOGE(TAG, "EMC2302 init failed");
    return ok;
}

void set_pct(int channel, float pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    uint8_t v = (uint8_t) (pct * 2.55f + 0.5f);
    i2c_bus::write_byte(EMC_ADDR, base_for(channel) + EMC_OFS_SETTING, v);
}

bool check()
{
    uint8_t cfg = 0;
    if (i2c_bus::read(EMC_ADDR, EMC_FAN2 + EMC_OFS_CONFIG1, &cfg, 1) != ESP_OK) return true;  // bus error, not a reset
    if (cfg == (0b01 << 3)) return true;
    ESP_LOGW(TAG, "fan controller config is %02x, it was reset - re-initialising", cfg);
    init();
    return false;
}

uint16_t rpm(int channel)
{
    uint8_t msb = 0, lsb = 0;
    uint8_t base = base_for(channel);
    if (i2c_bus::read(EMC_ADDR, base + EMC_OFS_TACH_MSB, &msb, 1) != ESP_OK) return 0;
    if (i2c_bus::read(EMC_ADDR, base + EMC_OFS_TACH_LSB, &lsb, 1) != ESP_OK) return 0;
    int raw = (msb << 5) | (lsb >> 3);
    if (raw == 0 || raw >= 8191) return 0;
    // 2-pole fan, 5 edges, 32.768 kHz tach clock
    int r = 60 * 32768 * 4 / 2 / raw;
    return r > 65535 ? 0 : (uint16_t) r;
}

} // namespace fan

// ---------------------------------------------------------------- TMP1075
#define TMP_BASE 0x48
#if AF_HEX_FAMILY
// Supra/Ultra Hex: 0x4a and 0x4b sit next to the chips; the regulator reports its
// own temperature over PMBus.
static const int ASIC_SENSOR_DEV[] = {2, 3};
#define TMP_VR_INDEX -1
#elif CONFIG_AF_BOARD_HX6
// HX-6: 0x48, 0x49, 0x4a all under the chips; the TPS53647 reads the regulator
static const int ASIC_SENSOR_DEV[] = {0, 1, 2};
#define TMP_VR_INDEX -1
#elif CONFIG_AF_BOARD_NEXUS
// Nexus S1 (from its stock firmware): 0x48 near the chips, 0x49 at the VR
static const int ASIC_SENSOR_DEV[] = {0};
#define TMP_VR_INDEX 1
#else
// NerdQaxe++: 0x48, 0x4a, 0x4b near the chips, 0x49 on the back of the VR
static const int ASIC_SENSOR_DEV[] = {0, 2, 3};
#define TMP_VR_INDEX 1
#endif
#define MAX_ASIC_SENSORS ((int) (sizeof(ASIC_SENSOR_DEV) / sizeof(ASIC_SENSOR_DEV[0])))

// NAN on a bus error or an implausible reading, never 0: a dead sensor must
// not look like a cool chip to the cut-off and the tuner.
static float tmp_read(int dev)
{
    uint8_t buf[2];
    if (i2c_bus::read(TMP_BASE + dev, 0x00, buf, 2) != ESP_OK) return NAN;
    uint16_t raw = (buf[0] << 8) | buf[1];
    if (raw > 0x7ff0) return NAN;  // below 0 C on a running miner means garbage
    return (raw >> 4) * 0.0625f;
}

static bool tmp_present(int dev)
{
    for (int tries = 0; tries < 3; tries++)
        if (!isnan(tmp_read(dev))) return true;
    return false;
}

static int s_num_asic_sensors = 0;

namespace temp {

int detect()
{
    // Which of the four TMP1075 addresses answer, once, so a board with fewer
    // sensors than its definition expects can be told from a dead one.
    char found[48] = "";
    for (int dev = 0; dev < 4; dev++) {
        bool here = i2c_bus::probe(TMP_BASE + dev);
        snprintf(found + strlen(found), sizeof(found) - strlen(found), "%s0x%02x%s", dev ? " " : "", TMP_BASE + dev,
                 here ? "" : "(none)");
    }
    ESP_LOGI(TAG, "TMP1075 probe: %s", found);

    // ASIC sensor i stands for the chips around it, so the list has to be used
    // in order: the count stops at the first one missing.
    s_num_asic_sensors = 0;
    for (int i = 0; i < MAX_ASIC_SENSORS; i++) {
        if (!tmp_present(ASIC_SENSOR_DEV[i])) break;
        s_num_asic_sensors++;
    }
    for (int i = s_num_asic_sensors + 1; i < MAX_ASIC_SENSORS; i++)
        if (tmp_present(ASIC_SENSOR_DEV[i]))
            ESP_LOGW(TAG, "TMP1075 at 0x%02x answers but 0x%02x before it does not: not used", TMP_BASE + ASIC_SENSOR_DEV[i],
                     TMP_BASE + ASIC_SENSOR_DEV[s_num_asic_sensors]);
    ESP_LOGI(TAG, "%d of %d ASIC temperature sensors", s_num_asic_sensors, MAX_ASIC_SENSORS);
    return s_num_asic_sensors;
}

float asic_area(int index)
{
    if (index < 0 || index >= s_num_asic_sensors) return NAN;
    return tmp_read(ASIC_SENSOR_DEV[index]);
}

float vr_board() { return TMP_VR_INDEX < 0 ? NAN : tmp_read(TMP_VR_INDEX); }

float chip_diode(int) { return NAN; }  // die temperature comes from the TMP451 mux here

int count() { return s_num_asic_sensors; }

} // namespace temp

#endif
