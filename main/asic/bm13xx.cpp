#include "bm13xx.h"

#include <math.h>
#include <string.h>
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "asic";

#define UART UART_NUM_1
#define PIN_TX AF_PIN_UART_TX
#define PIN_RX AF_PIN_UART_RX

#define TYPE_JOB 0x20
#define TYPE_CMD 0x40
#define GROUP_ALL 0x10
#define CMD_SETADDRESS 0x00
#define CMD_WRITE 0x01
#define CMD_READ 0x02
#define CMD_INACTIVE 0x03
#define CMD_WRITE_SINGLE (TYPE_CMD | CMD_WRITE)
#define CMD_WRITE_ALL (TYPE_CMD | GROUP_ALL | CMD_WRITE)
#define CMD_READ_ALL (TYPE_CMD | GROUP_ALL | CMD_READ)

#if AF_ASIC_BM1366
static const uint8_t CHIP_ID[6] = {0xaa, 0x55, 0x13, 0x66, 0x00, 0x00};
#elif AF_ASIC_BM1368
static const uint8_t CHIP_ID[6] = {0xaa, 0x55, 0x13, 0x68, 0x00, 0x00};
#elif AF_ASIC_BM1373
// The BM1373 answers with the BM1372's ID (13 72); accept 13 73 as well.
static const uint8_t CHIP_ID[6] = {0xaa, 0x55, 0x13, 0x72, 0x00, 0x00};
#else
static const uint8_t CHIP_ID[6] = {0xaa, 0x55, 0x13, 0x70, 0x00, 0x00};
#endif

static SemaphoreHandle_t s_tx_lock;
static float s_freq = 56.25f;
static uint32_t s_diff_mask = 0xffffffff;
static uint32_t s_version_mask = 0xffffffff;  // unknown: rewrite on next set_version_mask
static uint8_t s_addr_interval = 64;
#if AF_ASIC_BM1373
static uint8_t s_nonce_interval = 64;  // chip number from a nonce; differs from the address stride here
#endif

// ------------------------------------------------------------------ CRC
static uint8_t crc5(const uint8_t *data, int len)
{
    uint8_t c[5] = {1, 1, 1, 1, 1};
    for (int i = 0; i < len * 8; i++) {
        uint8_t din = (data[i / 8] >> (7 - (i % 8))) & 1;
        uint8_t n0 = c[4] ^ din;
        uint8_t n2 = c[1] ^ c[4] ^ din;
        c[4] = c[3];
        c[3] = c[2];
        c[2] = n2;
        c[1] = c[0];
        c[0] = n0;
    }
    return (c[4] << 4) | (c[3] << 3) | (c[2] << 2) | (c[1] << 1) | c[0];
}

static uint16_t crc16_false(const uint8_t *data, int len)
{
    uint16_t crc = 0xffff;
    for (int i = 0; i < len; i++) {
        crc ^= (uint16_t) data[i] << 8;
        for (int b = 0; b < 8; b++) crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
    return crc;
}

// ------------------------------------------------------------------ framing
static void send(uint8_t header, const uint8_t *data, uint8_t len)
{
    const bool is_job = header & TYPE_JOB;
    uint8_t buf[96];
    int total = len + (is_job ? 6 : 5);
    buf[0] = 0x55;
    buf[1] = 0xAA;
    buf[2] = header;
    buf[3] = len + (is_job ? 4 : 3);
    memcpy(buf + 4, data, len);
    if (is_job) {
        uint16_t c = crc16_false(buf + 2, len + 2);
        buf[4 + len] = c >> 8;
        buf[5 + len] = c & 0xff;
    } else {
        buf[4 + len] = crc5(buf + 2, len + 2);
    }
    xSemaphoreTake(s_tx_lock, portMAX_DELAY);
    uart_write_bytes(UART, buf, total);
    xSemaphoreGive(s_tx_lock);
}

static void send2(uint8_t header, uint8_t a, uint8_t b)
{
    uint8_t d[2] = {a, b};
    send(header, d, 2);
}

static void write_all(uint8_t reg, uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3)
{
    uint8_t d[6] = {0x00, reg, b0, b1, b2, b3};
    send(CMD_WRITE_ALL, d, 6);
}

static void write_one(uint8_t addr, uint8_t reg, uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3)
{
    uint8_t d[6] = {addr, reg, b0, b1, b2, b3};
    send(CMD_WRITE_SINGLE, d, 6);
}

[[maybe_unused]] static int next_pow2(int n)
{
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}

static uint8_t reverse_bits(uint8_t v)
{
    uint8_t r = 0;
    for (int i = 0; i < 8; i++) {
        r = (r << 1) | (v & 1);
        v >>= 1;
    }
    return r;
}

#if AF_ASIC_BM1368 || AF_ASIC_BM1366
// Reference PLL search for the BM1368 / BM1366: closest frequency, then lowest VCO,
// then the smallest post divider product.
static bool send_pll(float target)
{
    float min_diff = 1e9f, min_vco = 1e9f, best_f = 0;
    int min_post = 1 << 16, best_ref = 0, best_fb = 0, best_p1 = 0, best_p2 = 0;
    for (int ref = 2; ref > 0; ref--) {
        for (int p1 = 7; p1 > 0; p1--) {
            for (int p2 = 7; p2 > 0; p2--) {
                int div = ref * p1 * p2;
                int fb = (int) roundf(target / 25.0f * div);
                if (p1 <= p2 || fb < 144 || fb > 235) continue;
                float f = 25.0f * fb / div;
                float diff = fabsf(target - f);
                float vco = 25.0f * fb / ref;
                if (diff < min_diff || (fabsf(diff - min_diff) < 1e-4f && vco < min_vco) ||
                    (fabsf(diff - min_diff) < 1e-4f && fabsf(vco - min_vco) < 1e-4f && p1 * p2 < min_post)) {
                    min_diff = diff;
                    min_vco = vco;
                    min_post = p1 * p2;
                    best_ref = ref;
                    best_fb = fb;
                    best_p1 = p1;
                    best_p2 = p2;
                    best_f = f;
                }
            }
        }
    }
#else
// PLL search identical in output to the S21 register dumps.
static bool send_pll(float target)
{
    float min_diff = 2.0f;
    int best_ref = 0, best_fb = 0, best_p1 = 0, best_p2 = 0, pmin = 255, p2min = 255;
    float best_f = 0;
    for (int ref = 2; ref > 0; ref--) {
        for (int p1 = 7; p1 > 0; p1--) {
            for (int p2 = 7; p2 > 0; p2--) {
                int fb = (int) roundf(target / 25.0f * (ref * p2 * p1));
                float f = 25.0f * fb / (ref * p2 * p1);
                if (fb >= 0xa0 && fb <= 0xef && fabsf(target - f) <= min_diff && p1 >= p2 && p1 * p2 < pmin &&
                    p2 <= p2min) {
                    p2min = p2;
                    pmin = p1 * p2;
                    best_ref = ref;
                    best_fb = fb;
                    best_p1 = p1;
                    best_p2 = p2;
                    best_f = f;
                    min_diff = fabsf(target - f);
                }
            }
        }
    }
#endif
    if (!best_fb) {
        ESP_LOGE(TAG, "no PLL setting for %.2f MHz", target);
        return false;
    }
    uint8_t d[6] = {0x00, 0x08, (uint8_t) ((best_fb * 25 / best_ref >= 2400) ? 0x50 : 0x40), (uint8_t) best_fb,
                    (uint8_t) best_ref, (uint8_t) ((((best_p1 - 1) & 0xf) << 4) | ((best_p2 - 1) & 0xf))};
    send(CMD_WRITE_ALL, d, 6);
    s_freq = target;
    ESP_LOGD(TAG, "PLL %.2f MHz (actual %.2f)", target, best_f);
    return true;
}

static void set_version_mask_all()
{
    write_all(0xA4, 0x90, 0x00, 0xFF, 0xFF);
    s_version_mask = 0xffffffff;  // init rolls all 16 bits; the pool's mask is applied afterwards
}

namespace asic {

void uart_init()
{
    s_tx_lock = xSemaphoreCreateMutex();
    uart_config_t cfg = {};
    cfg.baud_rate = 115200;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;
    uart_param_config(UART, &cfg);
    uart_set_pin(UART, PIN_TX, PIN_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART, 2048, 2048, 0, NULL, 0);
}

int init(float freq_mhz, int expected_chips, uint32_t difficulty)
{
    // Freshly powered chips listen at 115200. After switch_to_fast_baud() the UART
    // stays at 1 Mbaud, so every later power-up (fault retry, overheat recovery)
    // found no chips unless the port is set back first.
    uart_set_baudrate(UART, 115200);
    uart_flush(UART);
    // The chips just came out of reset at their default PLL. Ramping from the
    // clock they ran at before a power cut would jump them from ~56 MHz to full
    // speed in one step; on the Supra Hex that surge trips the regulator's
    // over-current shutdown on every fault retry.
    s_freq = 56.25f;
#if AF_ASIC_BM1373
    // shufps writes the version mask with the enable bit clear before counting
    for (int i = 0; i < 4; i++) write_all(0xA4, 0x80, 0x00, 0xFF, 0xFF);
#else
    for (int i = 0; i < 4; i++) set_version_mask_all();
#endif

    // count chips: read reg 0 on everybody
    send2(CMD_READ_ALL, 0x00, 0x00);
    int chips = 0;
    uint8_t buf[11];
    while (uart_read_bytes(UART, buf, sizeof(buf), pdMS_TO_TICKS(1000)) == sizeof(buf)) {
#if AF_ASIC_BM1373
        bool match = memcmp(buf, CHIP_ID, 3) == 0 && (buf[3] == 0x72 || buf[3] == 0x73);
#else
        bool match = memcmp(buf, CHIP_ID, sizeof(CHIP_ID)) == 0;
#endif
        if (match) chips++;
        else ESP_LOGW(TAG, "unexpected reply during chip count");
    }
    if (chips == expected_chips) ESP_LOGI(TAG, "%d %s chips detected", chips, MODEL);
    else ESP_LOGE(TAG, "%d chips detected, expected %d", chips, expected_chips);
    if (!chips) return 0;

#if AF_ASIC_BM1366
    // BM1366 sequence from the Bitaxe / NerdQAxe reference firmware (S19XP dumps)
    set_version_mask_all();
    write_all(0xA8, 0x00, 0x07, 0x00, 0x00);
    write_all(0x18, 0xFF, 0x0F, 0xC1, 0x00);  // misc control
    send2(TYPE_CMD | GROUP_ALL | CMD_INACTIVE, 0x00, 0x00);
    s_addr_interval = 256 / next_pow2(chips);
    for (int i = 0; i < chips; i++) send2(TYPE_CMD | CMD_SETADDRESS, i * s_addr_interval, 0x00);
    write_all(0x3C, 0x80, 0x00, 0x85, 0x40);  // core register control
    write_all(0x3C, 0x80, 0x00, 0x80, 0x20);
    s_diff_mask = 0xffffffff;
    set_difficulty_mask(difficulty);
    write_all(0x54, 0x00, 0x00, 0x00, 0x03);  // analog mux
    write_all(0x58, 0x02, 0x11, 0x11, 0x11);  // IO driver strength
    for (int i = 0; i < chips; i++) {
        uint8_t a = i * s_addr_interval;
        write_one(a, 0xA8, 0x00, 0x07, 0x01, 0xF0);
        write_one(a, 0x18, 0xF0, 0x00, 0xC1, 0x00);
        write_one(a, 0x3C, 0x80, 0x00, 0x85, 0x40);
        write_one(a, 0x3C, 0x80, 0x00, 0x80, 0x20);
        write_one(a, 0x3C, 0x80, 0x00, 0x82, 0xAA);
    }
#elif AF_ASIC_BM1368
    // BM1368 sequence from the Bitaxe reference firmware (S21 register dumps)
    send2(TYPE_CMD | GROUP_ALL | CMD_INACTIVE, 0x00, 0x00);
    write_all(0xA8, 0x00, 0x07, 0x00, 0x00);
    write_all(0x18, 0xFF, 0x0F, 0xC1, 0x00);  // misc control
    write_all(0x3C, 0x80, 0x00, 0x8B, 0x00);  // core register control
    write_all(0x3C, 0x80, 0x00, 0x80, 0x18);
    write_all(0x14, 0x00, 0x00, 0x00, 0xFF);  // ticket mask, set properly below
    write_all(0x54, 0x00, 0x00, 0x00, 0x03);  // analog mux
    write_all(0x58, 0x02, 0x11, 0x11, 0x11);  // IO driver strength

    s_addr_interval = 256 / chips;
    for (int i = 0; i < chips; i++) send2(TYPE_CMD | CMD_SETADDRESS, i * s_addr_interval, 0x00);
    for (int i = 0; i < chips; i++) {
        uint8_t a = i * s_addr_interval;
        write_one(a, 0xA8, 0x00, 0x07, 0x01, 0xF0);
        write_one(a, 0x18, 0xF0, 0x00, 0xC1, 0x00);
        write_one(a, 0x3C, 0x80, 0x00, 0x8B, 0x00);
        write_one(a, 0x3C, 0x80, 0x00, 0x80, 0x18);
        write_one(a, 0x3C, 0x80, 0x00, 0x82, 0xAA);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    s_diff_mask = 0xffffffff;
    set_difficulty_mask(difficulty);

#elif AF_ASIC_BM1373
    // BM1373 sequence from shufps' NerdQAxe firmware (components/bm1397/bm1373.cpp),
    // which the Nexus S1's stock firmware is built on. bitaxe's differs in places
    // (3C clock delay 805A, broadcast A8, regs 0x19 and 0x0D); don't mix the two.
    write_all(0xA4, 0x80, 0x00, 0xFF, 0xFF);
    write_all(0xA8, 0x00, 0x07, 0x00, 0x00);
    write_all(0x18, 0xFF, 0x00, 0xC1, 0x00);  // misc control
    send2(TYPE_CMD | GROUP_ALL | CMD_INACTIVE, 0x00, 0x00);

    s_addr_interval = 16;  // both sources: a fixed stride of 16, not 256 / chips
    s_nonce_interval = 256 / next_pow2(chips);
    for (int i = 0; i < chips; i++) send2(TYPE_CMD | CMD_SETADDRESS, i * s_addr_interval, 0x00);

    write_all(0x3C, 0x80, 0x00, 0x80, 0x0C);  // core register control
    s_diff_mask = 0xffffffff;
    set_difficulty_mask(difficulty);
    write_all(0x58, 0x00, 0x01, 0x11, 0x11);  // IO driver strength
    write_all(0x68, 0x5A, 0xA5, 0x5A, 0xA5);
    // Single-chip writes go to chip * 4 although addresses were set at chip * 16:
    // the Nexus stock firmware and bitaxe both do this (shufps uses chip * 16).
    for (int i = 0; i < chips; i++) {
        uint8_t a = i * 4;
        write_one(a, 0xA8, 0x00, 0x07, 0x01, 0xF0);
        write_one(a, 0x18, 0xFF, 0x00, 0xC1, 0x00);
        write_one(a, 0x3C, 0x80, 0x00, 0x80, 0x0C);
        write_one(a, 0x3C, 0x80, 0x00, 0x82, 0xAA);
    }
    write_all(0x54, 0x00, 0x00, 0x00, 0x02);  // analog mux

#else
    set_version_mask_all();
    write_all(0xA8, 0x00, 0x07, 0x00, 0x00);
    write_all(0x18, 0xF0, 0x00, 0xC1, 0x00);  // misc control
    send2(TYPE_CMD | GROUP_ALL | CMD_INACTIVE, 0x00, 0x00);

    s_addr_interval = 256 / next_pow2(chips);
    for (int i = 0; i < chips; i++) send2(TYPE_CMD | CMD_SETADDRESS, i * s_addr_interval, 0x00);

    write_all(0x3C, 0x80, 0x00, 0x8B, 0x00);  // core register control
    write_all(0x3C, 0x80, 0x00, 0x80, 0x0C);
    s_diff_mask = 0xffffffff;
    set_difficulty_mask(difficulty);
    write_all(0x58, 0x00, 0x01, 0x11, 0x11);  // IO driver strength
    write_all(0x68, 0x5A, 0xA5, 0x5A, 0xA5);

    for (int i = 0; i < chips; i++) {
        uint8_t a = i * s_addr_interval;
        write_one(a, 0xA8, 0x00, 0x07, 0x01, 0xF0);
        write_one(a, 0x18, 0xF0, 0x00, 0xC1, 0x00);
        write_one(a, 0x3C, 0x80, 0x00, 0x8B, 0x00);
        write_one(a, 0x3C, 0x80, 0x00, 0x80, 0x0C);
        write_one(a, 0x3C, 0x80, 0x00, 0x82, 0xAA);
    }
    write_all(0xB9, 0x00, 0x00, 0x44, 0x80);
    write_all(0x54, 0x00, 0x00, 0x00, 0x02);  // analog mux
    write_all(0xB9, 0x00, 0x00, 0x44, 0x80);
    write_all(0x3C, 0x80, 0x00, 0x8D, 0xEE);

#endif
    set_frequency(freq_mhz);
#if AF_ASIC_BM1366
    write_all(0x10, 0x00, 0x00, 0x15, 0x1C);  // hash counting (S19XP stock value)
#elif AF_ASIC_BM1368
    write_all(0x10, 0x00, 0x00, 0x15, 0xA4);  // hash counting (S21 stock value)
#elif AF_ASIC_BM1373
    // nonce space: bitaxe's S21 Pro stock value, which stops BM1372/1373 chips
    // searching overlapping work (shufps inherits the BM1368's 0x15A4 by accident)
    write_all(0x10, 0x00, 0x00, 0x1E, 0xB5);
#elif defined(AF_VR_FREQ_HZ)
    {
        // reg = 65536 * 3000 Hz (15 MHz IO clock / 5000) / rolling frequency
        uint32_t v = (uint32_t) ((65536ull * 3000 + AF_VR_FREQ_HZ / 2) / AF_VR_FREQ_HZ);
        write_all(0x10, v >> 24, (v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff);
    }
#else
    write_all(0x10, 0x00, 0x00, 0x1E, 0xB5);  // version rolling speed (stock value)
#endif
    set_version_mask_all();
    return chips;
}

void switch_to_fast_baud()
{
    write_all(0x28, 0x11, 0x30, 0x02, 0x00);  // 1 Mbaud
    vTaskDelay(pdMS_TO_TICKS(500));
    uart_set_baudrate(UART, 1000000);
    uart_flush(UART);
}

bool set_frequency(float target)
{
    const float step = 6.25f;
    float cur = s_freq;
    if (fmodf(cur, step) != 0) {
        cur = (target > cur) ? ceilf(cur / step) * step : floorf(cur / step) * step;
        if (!send_pll(cur)) return false;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    while (fabsf(target - cur) > 0.01f) {
        float d = fminf(step, fabsf(target - cur));
        cur += (target > cur) ? d : -d;
        if (!send_pll(cur)) return false;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ESP_LOGI(TAG, "frequency %.2f MHz", target);
    return send_pll(target);
}

float frequency() { return s_freq; }

// Version bits the chips may roll, as a block-header mask (bits 13-28 only).
void set_version_mask(uint32_t mask)
{
    uint16_t bits = (uint16_t) (mask >> 13);
    mask = (uint32_t) bits << 13;
    if (mask == s_version_mask) return;
    write_all(0xA4, 0x90, 0x00, bits >> 8, bits & 0xff);
    s_version_mask = mask;
    ESP_LOGI(TAG, "version mask %08lx", (unsigned long) mask);
}

void set_difficulty_mask(uint32_t difficulty)
{
    uint32_t p = 1;
    while ((p << 1) <= difficulty && p < 0x80000000u) p <<= 1;
    uint32_t mask = p - 1;
    if (mask == s_diff_mask) return;
    uint8_t d[6] = {0x00, 0x14, 0, 0, 0, 0};
    for (int i = 0; i < 4; i++) d[5 - i] = reverse_bits((mask >> (8 * i)) & 0xff);
    send(CMD_WRITE_ALL, d, 6);
    s_diff_mask = mask;
    ESP_LOGI(TAG, "ticket mask %lu", (unsigned long) mask);
}

uint8_t send_job(uint32_t counter, AsicJob &job)
{
#if AF_ASIC_BM1366
    job.job_id = (counter * 8) & 0x7f;  // the BM1366 reports it back with the small-core id in the low 3 bits
#else
    job.job_id = (counter * 24) & 0x7f;
#endif
    job.num_midstates = 1;
    send(TYPE_JOB | CMD_WRITE, (const uint8_t *) &job, sizeof(job));
    return job.job_id;
}

bool receive(AsicResult &out, int timeout_ms)
{
    uint8_t b[11];
    int n = uart_read_bytes(UART, b, sizeof(b), pdMS_TO_TICKS(timeout_ms));
    if (n <= 0) return false;
    if (n != 11 || b[0] != 0xAA || b[1] != 0x55) {
        uart_flush_input(UART);
        return false;
    }
    uint32_t nonce;
    memcpy(&nonce, b + 2, 4);
    uint8_t midstate = b[6];
    uint8_t job_id = b[7];
    uint16_t version = (b[8] << 8) | b[9];  // big endian on the wire
    uint8_t crc = b[10];

    if (!(crc & 0x80)) {
        out.is_register = true;
        out.data = __builtin_bswap32(nonce);
        out.reg = job_id;
        out.chip = s_addr_interval ? midstate / s_addr_interval : 0;
        return true;
    }
    out.is_register = false;
#if AF_ASIC_BM1366
    out.job_slot = job_id & 0xf8;
#else
    out.job_slot = (job_id & 0xf0) >> 1;
#endif
    out.nonce = nonce;
    out.version_bits = (uint32_t) version << 13;
    uint32_t nh = __builtin_bswap32(nonce);
#if AF_ASIC_BM1373
    out.chip = s_nonce_interval ? ((nh >> 17) & 0xff) / s_nonce_interval : 0;  // bitaxe; shufps: (nh >> 24) & 3
#else
    out.chip = s_addr_interval ? ((nh >> 17) & 0xff) / s_addr_interval : 0;
#endif
    return true;
}

void read_register_all(uint8_t reg) { send2(CMD_READ_ALL, 0x00, reg); }

void reset_register_all(uint8_t reg) { write_all(reg, 0, 0, 0, 0); }

} // namespace asic
