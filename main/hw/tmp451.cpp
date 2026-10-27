#include "tmp451.h"

#include <math.h>
#include "core/board_config.h"

#if AF_TMP451_MUXES > 0

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"

static const char *TAG = "tmp451";

#define PIN_A0 GPIO_NUM_2
#define PIN_A1 ((gpio_num_t) AF_TMP451_PIN_A1)
static const uint8_t ADDR[2] = {0x4c, 0x4e};  // mux 0: chips 0-3, mux 1: chips 4-7

#define REG_LOCAL_MSB 0x00
#define REG_REMOTE_MSB 0x01
#define REG_STATUS 0x02  // bit 7: converting
#define REG_CONFIG1_W 0x09
#define REG_ONE_SHOT_W 0x0F
#define REG_REMOTE_LSB 0x10
#define REG_RTOFFS_MSB 0x11
#define REG_RTOFFS_LSB 0x12
#define REG_NFACTOR_W 0x23

#define SWITCH_SETTLE_MS 50
#define READ_SETTLE_MS 75
#define STALE_US (15 * 1000000LL)  // older than this counts as a lost sensor

static int s_muxes = 0;
static float s_temp[8];
static int64_t s_when[8];
static volatile bool s_enabled = false;
static volatile uint32_t s_gen = 0;  // bumped by enable(): a conversion from before is thrown away

static bool wr(uint8_t addr, uint8_t reg, uint8_t v) { return i2c_bus::write_byte(addr, reg, v) == ESP_OK; }
static bool rd(uint8_t addr, uint8_t reg, uint8_t *v) { return i2c_bus::read(addr, reg, v, 1) == ESP_OK; }

static void select(int ch)
{
    gpio_set_level(PIN_A0, ch & 1);
    gpio_set_level(PIN_A1, (ch >> 1) & 1);
}

static float one_shot_remote(uint8_t addr)
{
    if (!wr(addr, REG_ONE_SHOT_W, 0xff)) return NAN;
    int64_t t0 = esp_timer_get_time();
    uint8_t st = 0x80;
    while (true) {
        if (!rd(addr, REG_STATUS, &st)) return NAN;
        if (!(st & 0x80)) break;
        if (esp_timer_get_time() - t0 > 200000) return NAN;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (st & 0x04) return NAN;  // OPEN: no diode on this channel (chip unpowered or not fitted)
    uint8_t msb = 0, lsb = 0;
    if (!rd(addr, REG_REMOTE_MSB, &msb) || !rd(addr, REG_REMOTE_LSB, &lsb)) return NAN;
    return (int8_t) msb + ((lsb >> 4) & 0x0f) * 0.0625f;
}

// Calibration from the reference firmware: the diodes read low and compressed.
static float correct(float t) { return isnan(t) ? NAN : (t - 30.0f) * 1.09f + 30.0f - 29.5f; }

static void reader_task(void *)
{
    while (true) {
        if (!s_enabled) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        for (int ch = 0; ch < 4 && s_enabled; ch++) {
            uint32_t gen = s_gen;
            select(ch);  // both muxes share the select lines
            vTaskDelay(pdMS_TO_TICKS(SWITCH_SETTLE_MS));
            for (int m = 0; m < s_muxes; m++) one_shot_remote(ADDR[m]);  // first one after a switch is thrown away
            vTaskDelay(pdMS_TO_TICKS(READ_SETTLE_MS));
            for (int m = 0; m < s_muxes; m++) {
                float t = correct(one_shot_remote(ADDR[m]));
                // a diode that is open or shorted reads far outside this range
                if (!isnan(t) && t > -10 && t < 150 && s_enabled && gen == s_gen) {
                    s_temp[m * 4 + ch] = t;
                    s_when[m * 4 + ch] = esp_timer_get_time();
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

namespace tmp451 {

int start()
{
    gpio_config_t g = {};
    g.mode = GPIO_MODE_OUTPUT;
    g.pin_bit_mask = (1ULL << PIN_A0) | (1ULL << PIN_A1);
    gpio_config(&g);
    select(0);
    for (int m = 0; m < AF_TMP451_MUXES; m++) {
        uint8_t st;
        if (!rd(ADDR[m], REG_STATUS, &st)) break;  // the second mux only counts if the first is there
        // shutdown mode (one-shot conversions), default ideality, no offset
        wr(ADDR[m], REG_CONFIG1_W, 1 << 6);
        wr(ADDR[m], REG_NFACTOR_W, 0x00);
        wr(ADDR[m], REG_RTOFFS_MSB, 0x00);
        wr(ADDR[m], REG_RTOFFS_LSB, 0x00);
        s_muxes++;
        ESP_LOGI(TAG, "mux %d at 0x%02x: chips %d-%d", m, ADDR[m], m * 4 + 1, m * 4 + 4);
    }
    if (s_muxes < AF_TMP451_MUXES) ESP_LOGW(TAG, "%d of %d die temperature muxes found", s_muxes, AF_TMP451_MUXES);
    for (int i = 0; i < 8; i++) s_temp[i] = NAN;
    if (s_muxes) xTaskCreate(reader_task, "tmp451", 3072, NULL, 5, NULL);
    return chips();
}

void enable(bool on)
{
    s_gen = s_gen + 1;  // enable() is the only writer
    for (int i = 0; i < 8; i++) s_when[i] = 0;
    s_enabled = on;
}

int chips()
{
    int n = s_muxes * 4;
    return n > AF_NUM_ASICS ? AF_NUM_ASICS : n;
}

float chip(int index)
{
    if (index < 0 || index >= chips()) return NAN;
    if (s_when[index] == 0 || esp_timer_get_time() - s_when[index] > STALE_US) return NAN;
    return s_temp[index];
}

} // namespace tmp451

#else

namespace tmp451 {
int start() { return 0; }
int chips() { return 0; }
void enable(bool) {}
float chip(int) { return NAN; }
}

#endif
