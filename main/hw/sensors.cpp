#include "sensors.h"

#include <math.h>
#include "esp_log.h"
#include "i2c_bus.h"

static const char *TAG = "sensors";

// ---------------------------------------------------------------- EMC2302
#define EMC_ADDR 0x2e
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
#define TMP_VR_INDEX 1

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
    s_num_asic_sensors = 0;
    for (int i = 0; i < 4; i++) {
        if (i == TMP_VR_INDEX) continue;
        if (!tmp_present(i)) break;
        s_num_asic_sensors++;
    }
    ESP_LOGI(TAG, "%d ASIC temperature sensors", s_num_asic_sensors);
    return s_num_asic_sensors;
}

float asic_area(int index)
{
    if (index >= s_num_asic_sensors) return NAN;
    // sensors live at 0x48, 0x4a, 0x4b (0x49 is the VR sensor)
    return tmp_read(index + (index ? 1 : 0));
}

float vr_board() { return tmp_read(TMP_VR_INDEX); }

int count() { return s_num_asic_sensors; }

} // namespace temp
