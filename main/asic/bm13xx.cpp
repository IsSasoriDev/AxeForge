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
#elif AF_ASIC_BM1397
static const uint8_t CHIP_ID[4] = {0xaa, 0x55, 0x13, 0x97};
#elif AF_ASIC_BM1373
// answers as 0x1372 or 0x1373; bitaxe reads a 9-byte reply, the Nexus sends 11:
// the chip count scans the byte stream instead of reading fixed frames
static const uint8_t CHIP_ID[4] = {0xaa, 0x55, 0x13, 0x72};
#define CHIP_ID_ALT 0x73
#else
static const uint8_t CHIP_ID[6] = {0xaa, 0x55, 0x13, 0x70, 0x00, 0x00};
#endif
#if AF_ASIC_BM1397
#define CHIP_ID_FRAME 9  // the BM1397 answers in 9-byte frames, results included
#else
#define CHIP_ID_FRAME 11
#endif

static SemaphoreHandle_t s_tx_lock;
static float s_freq = 56.25f;
static uint32_t s_diff_mask = 0xffffffff;
static uint32_t s_version_mask = 0xffffffff;
#if !(AF_ASIC_BM1366 || AF_ASIC_BM1368 || AF_ASIC_BM1373 || AF_ASIC_BM1397)
static uint32_t s_vr_hz = 0;  // version rolling speed override, 0 = default
#endif  // unknown: rewrite on next set_version_mask
static uint8_t s_addr_interval = 64;   // chip address step: SETADDRESS, writes, register replies
static uint8_t s_nonce_interval = 64;  // chip number in a nonce = nonce bits 17-24 / this
#if AF_ASIC_BM1373
static uint8_t s_cmd_interval = 4;     // the BM1373 addresses single-chip writes at 1/4 of its step
#endif
static SemaphoreHandle_t s_pll_lock;   // chain clock and per-chip offsets change together
static int s_chips = 0;
static int8_t s_off[AF_NUM_ASICS];      // wanted per-chip offsets (MHz)
static int8_t s_applied[AF_NUM_ASICS];  // offsets the chips run at now

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
    uint8_t buf[160];  // a BM1397 job with four midstates is 152 bytes
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

[[maybe_unused]] static void write_one(uint8_t addr, uint8_t reg, uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3)
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

#if AF_ASIC_BM1397
// Reference PLL search for the BM1397 (pll.c): feedback divider 60..200, post
// divider 1 above post divider 2, closest frequency, then lowest VCO, then the
// smallest post divider product. The chip wants its PLL0 divider written first.
static bool send_pll(float target, int chip = -1)
{
    (void) chip;  // one chip on the Max: no per-chip clocks
    float min_diff = 1e9f, min_vco = 1e9f, best_f = 0;
    int min_post = 1 << 16, best_ref = 0, best_fb = 0, best_p1 = 0, best_p2 = 0;
    for (int ref = 2; ref > 0; ref--) {
        for (int p1 = 7; p1 > 0; p1--) {
            for (int p2 = 7; p2 > 0; p2--) {
                int div = ref * p1 * p2;
                int fb = (int) roundf(target / 25.0f * div);
                if (p1 <= p2 || fb < 60 || fb > 200) continue;
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
    if (!best_fb) {
        ESP_LOGE(TAG, "no PLL setting for %.2f MHz", target);
        return false;
    }
    const uint8_t prediv[6] = {0x00, 0x70, 0x0F, 0x0F, 0x0F, 0x00};  // PLL0 divider
    const uint8_t pll[6] = {0x00, 0x08, 0x40, (uint8_t) best_fb, (uint8_t) best_ref,
                            (uint8_t) (((best_p1 & 0x7) << 4) | (best_p2 & 0x7))};
    for (int i = 0; i < 2; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
        send(CMD_WRITE_ALL, prediv, 6);
    }
    for (int i = 0; i < 2; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
        send(CMD_WRITE_ALL, pll, 6);
    }
    s_freq = target;
    ESP_LOGD(TAG, "PLL %.2f MHz (actual %.2f)", target, best_f);
    return true;
}
#else
#if AF_ASIC_BM1368 || AF_ASIC_BM1366
// Reference PLL search for the BM1368 / BM1366: closest frequency, then lowest VCO,
// then the smallest post divider product.
static bool send_pll(float target, int chip = -1)
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
static bool send_pll(float target, int chip = -1)
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
    if (chip >= 0) {
#if AF_ASIC_BM1373
        d[0] = (uint8_t) (chip * s_cmd_interval);
#else
        d[0] = (uint8_t) (chip * s_addr_interval);
#endif
        send(CMD_WRITE_SINGLE, d, 6);
        return true;
    }
    send(CMD_WRITE_ALL, d, 6);
    s_freq = target;
    ESP_LOGD(TAG, "PLL %.2f MHz (actual %.2f)", target, best_f);
    return true;
}
#endif  // AF_ASIC_BM1397

// One chip from one clock to another in 6.25 MHz steps, like the chain ramp.
static void ramp_chip(int chip, float from, float to)
{
    while (fabsf(to - from) > 0.01f) {
        float d = fminf(6.25f, fabsf(to - from));
        from += (to > from) ? d : -d;
        send_pll(from, chip);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void apply_offsets()
{
    for (int i = 0; i < s_chips && i < AF_NUM_ASICS; i++) {
        if (s_applied[i] == s_off[i]) continue;
        ramp_chip(i, s_freq + s_applied[i], s_freq + s_off[i]);
        s_applied[i] = s_off[i];
    }
}

static void clear_applied()
{
    for (int i = 0; i < s_chips && i < AF_NUM_ASICS; i++) {
        if (!s_applied[i]) continue;
        ramp_chip(i, s_freq + s_applied[i], s_freq);
        s_applied[i] = 0;
    }
}

static void set_version_mask_all()
{
#if AF_ASIC_BM1397
    return;  // no hardware version rolling: the midstates carry the versions
#endif
    write_all(0xA4, 0x90, 0x00, 0xFF, 0xFF);
    s_version_mask = 0xffffffff;  // init rolls all 16 bits; the pool's mask is applied afterwards
}

namespace asic {

void uart_init()
{
    s_tx_lock = xSemaphoreCreateMutex();
    s_pll_lock = xSemaphoreCreateRecursiveMutex();
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
    s_chips = 0;
    memset(s_applied, 0, sizeof(s_applied));  // reset chips run the chain clock
    // The chips just came out of reset at their default PLL. Ramping from the
    // clock they ran at before a power cut would jump them from ~56 MHz to full
    // speed in one step; on the Supra Hex that surge trips the regulator's
    // over-current shutdown on every fault retry.
#if AF_ASIC_BM1373 && AF_BM1373_SHUFPS_INIT
    s_freq = 56.25f;
    // shufps writes the version mask with the enable bit clear before counting
    for (int i = 0; i < 4; i++) write_all(0xA4, 0x80, 0x00, 0xFF, 0xFF);
#elif AF_ASIC_BM1373
    s_freq = 50.0f;  // the BM1373's reset clock (bitaxe)
#else
    s_freq = 56.25f;
    for (int i = 0; i < 4; i++) set_version_mask_all();
#endif

    // count chips: read reg 0 on everybody
    send2(CMD_READ_ALL, 0x00, 0x00);
    int chips = 0;
#ifdef CHIP_ID_ALT
    {
        // everything the chain sends until it goes quiet, then count the ID pattern
        uint8_t rx[256];
        int got = 0, r;
        while (got < (int) sizeof(rx) && (r = uart_read_bytes(UART, rx + got, sizeof(rx) - got, pdMS_TO_TICKS(1000))) > 0)
            got += r;
        for (int i = 0; i + 4 <= got; i++)
            if (memcmp(rx + i, CHIP_ID, 3) == 0 && (rx[i + 3] == CHIP_ID[3] || rx[i + 3] == CHIP_ID_ALT)) chips++;
    }
#else
    uint8_t buf[CHIP_ID_FRAME];
    while (uart_read_bytes(UART, buf, sizeof(buf), pdMS_TO_TICKS(1000)) == sizeof(buf)) {
        if (memcmp(buf, CHIP_ID, sizeof(CHIP_ID)) == 0) chips++;
        else ESP_LOGW(TAG, "unexpected reply during chip count");
    }
#endif
    if (chips == expected_chips) ESP_LOGI(TAG, "%d %s chips detected", chips, MODEL);
    else ESP_LOGE(TAG, "%d chips detected, expected %d", chips, expected_chips);
    if (!chips) return 0;
    s_chips = chips;

#if AF_ASIC_BM1373 && AF_BM1373_SHUFPS_INIT
    // BM1373 sequence from shufps' NerdQAxe firmware (components/bm1397/bm1373.cpp),
    // which the Nexus S1's stock firmware is built on. bitaxe's differs in places
    // (3C clock delay 805A, broadcast A8, regs 0x19 and 0x0D); don't mix the two.
    // Contributed by cbyam and run on a Nexus S1.
    write_all(0xA4, 0x80, 0x00, 0xFF, 0xFF);
    write_all(0xA8, 0x00, 0x07, 0x00, 0x00);
    write_all(0x18, 0xFF, 0x00, 0xC1, 0x00);  // misc control
    send2(TYPE_CMD | GROUP_ALL | CMD_INACTIVE, 0x00, 0x00);

    s_addr_interval = 16;  // both sources: a fixed stride of 16, not 256 / chips
    s_cmd_interval = 4;    // single-chip writes at chip * 4 (stock firmware and bitaxe)
    s_nonce_interval = 256 / next_pow2(chips);
    for (int i = 0; i < chips; i++) send2(TYPE_CMD | CMD_SETADDRESS, i * s_addr_interval, 0x00);

    write_all(0x3C, 0x80, 0x00, 0x80, 0x0C);  // core register control
    s_diff_mask = 0xffffffff;
    set_difficulty_mask(difficulty);
    write_all(0x58, 0x00, 0x01, 0x11, 0x11);  // IO driver strength
    write_all(0x68, 0x5A, 0xA5, 0x5A, 0xA5);
    for (int i = 0; i < chips; i++) {
        uint8_t a = i * s_cmd_interval;
        write_one(a, 0xA8, 0x00, 0x07, 0x01, 0xF0);
        write_one(a, 0x18, 0xFF, 0x00, 0xC1, 0x00);
        write_one(a, 0x3C, 0x80, 0x00, 0x80, 0x0C);
        write_one(a, 0x3C, 0x80, 0x00, 0x82, 0xAA);
    }
    write_all(0x54, 0x00, 0x00, 0x00, 0x02);  // analog mux
#elif AF_ASIC_BM1373
    // BM1373 (BM1372 family) sequence from the Bitaxe reference firmware: chain
    // inactive, addresses 0x10 apart, then IO, UART at 3 Mbaud, soft reset and the
    // staged core setup. The UART switch happens here, not in switch_to_fast_baud().
    send2(TYPE_CMD | GROUP_ALL | CMD_INACTIVE, 0x00, 0x00);
    vTaskDelay(pdMS_TO_TICKS(10));
    s_addr_interval = 0x10;
    s_cmd_interval = 0x10 >> 2;
    s_nonce_interval = 256 / chips;
    for (int i = 0; i < chips; i++) send2(TYPE_CMD | CMD_SETADDRESS, i * s_addr_interval, 0x00);
    vTaskDelay(pdMS_TO_TICKS(10));
    write_all(0x3C, 0x80, 0x00, 0x8D, 0x04);  // core reg 0x0D: nonce bin overflow off
    write_all(0x58, 0x00, 0x01, 0x11, 0x11);  // IO driver strength
    // the last chip of every voltage domain drives the next domain's input harder
    for (int d = 0; d < AF_VOLTAGE_DOMAINS; d++) {
        int last = ((d + 1) * chips) / AF_VOLTAGE_DOMAINS - 1;
        if (last >= (d * chips) / AF_VOLTAGE_DOMAINS)
            write_one((uint8_t) (last * s_cmd_interval), 0x58, 0x00, 0x01, 0xF1, 0x11);
    }
    write_all(0x68, 0x5A, 0xA5, 0x5A, 0xA5);  // ring oscillator pads off
    vTaskDelay(pdMS_TO_TICKS(10));
    write_all(0xC4, 0x00, 0x00, 0x00, 0x00);  // auto work config
    write_all(0x28, 0x80, 0x00, 0x00, 0x00);  // fast UART: 3 Mbaud
    uart_wait_tx_done(UART, pdMS_TO_TICKS(100));
    uart_set_baudrate(UART, 3000000);
    uart_flush(UART);
    vTaskDelay(pdMS_TO_TICKS(10));
    write_all(0xA8, 0xF0, 0x07, 0x01, 0xF0);  // soft reset
    vTaskDelay(pdMS_TO_TICKS(20));
    write_all(0x18, 0xFF, 0x00, 0xC1, 0x00);  // misc control: RNO enable
    vTaskDelay(pdMS_TO_TICKS(10));
    write_all(0x19, 0x00, 0x00, 0x00, 0x00);
    vTaskDelay(pdMS_TO_TICKS(10));
    write_all(0x3C, 0x80, 0x00, 0x8B, 0x00);  // core reg 0x0B: clock select
    vTaskDelay(pdMS_TO_TICKS(10));
    write_all(0x3C, 0x80, 0x00, 0x80, 0x5A);  // core reg 0x00: clock delay
    vTaskDelay(pdMS_TO_TICKS(10));
    write_all(0x3C, 0x80, 0x00, 0x82, 0xAA);  // core reg 0x02: cores on
    vTaskDelay(pdMS_TO_TICKS(10));
    write_all(0x54, 0x00, 0x00, 0x00, 0x02);  // analog mux: temperature diode
    s_diff_mask = 0xffffffff;
    set_difficulty_mask(difficulty);
#elif AF_ASIC_BM1397
    // BM1397 sequence from the Bitaxe reference firmware (bm1397.c)
    vTaskDelay(pdMS_TO_TICKS(20));
    send2(TYPE_CMD | GROUP_ALL | CMD_INACTIVE, 0x00, 0x00);
    s_addr_interval = 256 / chips;
    for (int i = 0; i < chips; i++) send2(TYPE_CMD | CMD_SETADDRESS, i * s_addr_interval, 0x00);
    write_all(0x80, 0x00, 0x00, 0x00, 0x00);  // clock order control 0
    write_all(0x84, 0x00, 0x00, 0x00, 0x00);  // clock order control 1
    write_all(0x20, 0x00, 0x00, 0x00, 0x01);  // ordered clock enable
    write_all(0x3C, 0x80, 0x00, 0x80, 0x74);  // core register control
    write_all(0x68, 0xC0, 0x70, 0x01, 0x11);  // PLL3 parameter
    write_all(0x28, 0x06, 0x00, 0x00, 0x0F);  // fast UART configuration
    write_all(0x18, 0x00, 0x00, 0x7A, 0x31);  // misc control: UART divider 26, 115,749 baud
    s_diff_mask = 0xffffffff;
    set_difficulty_mask(difficulty);
#elif AF_ASIC_BM1366
    // BM1366 sequence from the Bitaxe / NerdQAxe reference firmware (S19XP dumps)
    set_version_mask_all();
    write_all(0xA8, 0x00, 0x07, 0x00, 0x00);
    write_all(0x18, 0xFF, 0x0F, 0xC1, 0x00);  // misc control
    send2(TYPE_CMD | GROUP_ALL | CMD_INACTIVE, 0x00, 0x00);
    // 256 / chips as the reference firmware does (42 on the 6-chip Ultra Hex);
    // the same as a power-of-two stride for the 1 and 4 chip boards
    s_addr_interval = 256 / chips;
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
#if !AF_ASIC_BM1373
    s_nonce_interval = s_addr_interval;
#endif
    set_frequency(freq_mhz);
#if AF_ASIC_BM1373
    write_all(0x10, 0x00, 0x00, 0x1E, 0xB5);  // nonce space (S21 Pro stock value; the generic one overlaps)
#elif AF_ASIC_BM1366
    write_all(0x10, 0x00, 0x00, 0x15, 0x1C);  // hash counting (S19XP stock value)
#elif AF_ASIC_BM1368
    write_all(0x10, 0x00, 0x00, 0x15, 0xA4);  // hash counting (S21 stock value)
#elif AF_ASIC_BM1397
    // nothing more: no version rolling, no rolling speed
#else
    set_vr_freq(s_vr_hz, true);  // version rolling speed: the user's, the board's or stock
#endif
    set_version_mask_all();
    return chips;
}

bool set_vr_freq(uint32_t hz, bool write)
{
#if AF_ASIC_BM1366 || AF_ASIC_BM1368 || AF_ASIC_BM1373 || AF_ASIC_BM1397
    (void) hz;
    (void) write;
    return false;  // register 0x10 sets the nonce space on these chips, not a rolling speed
#else
    s_vr_hz = hz;
    if (!write) return true;
#ifdef AF_VR_FREQ_HZ
    if (!hz) hz = AF_VR_FREQ_HZ;
#endif
    if (!hz) {
        write_all(0x10, 0x00, 0x00, 0x1E, 0xB5);  // version rolling speed (stock value, ~25 kHz)
        return true;
    }
    // reg = 65536 * 3000 Hz (15 MHz IO clock / 5000) / rolling frequency
    uint32_t v = (uint32_t) ((65536ull * 3000 + hz / 2) / hz);
    write_all(0x10, v >> 24, (v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff);
    return true;
#endif
}

void switch_to_fast_baud()
{
#if AF_ASIC_BM1373 && !AF_BM1373_SHUFPS_INIT
    return;  // bitaxe's sequence: already at 3 Mbaud since init
#endif
#if AF_ASIC_BM1397
    write_all(0x18, 0x00, 0x00, 0x60, 0x31);  // misc control: UART divider 0, 3.125 Mbaud
    vTaskDelay(pdMS_TO_TICKS(500));
    uart_set_baudrate(UART, 3125000);
    uart_flush(UART);
    return;
#endif
    write_all(0x28, 0x11, 0x30, 0x02, 0x00);  // 1 Mbaud
    vTaskDelay(pdMS_TO_TICKS(500));
    uart_set_baudrate(UART, 1000000);
    uart_flush(UART);
}

static bool ramp_chain(float target)
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

bool set_frequency(float target)
{
    // chips with an offset come back to the chain clock first, so the whole chain
    // ramps from one clock, then they step out to their own again
    xSemaphoreTakeRecursive(s_pll_lock, portMAX_DELAY);
    clear_applied();
    bool ok = ramp_chain(target);
    if (ok) apply_offsets();
    xSemaphoreGiveRecursive(s_pll_lock);
    return ok;
}

void set_chip_offsets(const int8_t *mhz, bool write)
{
    if (!s_pll_lock) {  // at boot, before uart_init(): only remembered for the chip init
        for (int i = 0; i < AF_NUM_ASICS; i++) s_off[i] = PER_CHIP_CLOCK ? mhz[i] : 0;
        return;
    }
    xSemaphoreTakeRecursive(s_pll_lock, portMAX_DELAY);
    for (int i = 0; i < AF_NUM_ASICS; i++) s_off[i] = PER_CHIP_CLOCK ? mhz[i] : 0;
    if (write) {
        apply_offsets();
        int8_t on[AF_NUM_ASICS];
        memcpy(on, s_applied, sizeof(on));
        char line[8 * AF_NUM_ASICS + 1] = "";
        for (int i = 0; i < s_chips && i < AF_NUM_ASICS; i++)
            snprintf(line + strlen(line), sizeof(line) - strlen(line), "%s%+d", i ? " " : "", on[i]);
        ESP_LOGI(TAG, "per-chip offsets (MHz): %s", line);
    }
    xSemaphoreGiveRecursive(s_pll_lock);
}

int chip_offset(int chip) { return chip >= 0 && chip < AF_NUM_ASICS ? s_applied[chip] : 0; }

int offset_total()
{
    int t = 0;
    for (int i = 0; i < s_chips && i < AF_NUM_ASICS; i++) t += s_applied[i];
    return t;
}

float frequency() { return s_freq; }

// Version bits the chips may roll, as a block-header mask (bits 13-28 only).
void set_version_mask(uint32_t mask)
{
#if AF_ASIC_BM1397
    (void) mask;
    return;  // the midstates carry the rolled versions
#endif
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
#if AF_ASIC_BM1397
    // 32 slots, 4 apart: the result carries the midstate index in the low 2 bits
    job.job_id = (counter * 4) & 0x7f;
#elif AF_ASIC_BM1366
    job.job_id = (counter * 8) & 0x7f;  // the BM1366 reports it back with the small-core id in the low 3 bits
#else
    job.job_id = (counter * 24) & 0x7f;
#endif
#if !AF_ASIC_BM1397
    job.num_midstates = 1;  // the BM1397's count comes from work::build
#endif
    send(TYPE_JOB | CMD_WRITE, (const uint8_t *) &job, sizeof(job));
    return job.job_id;
}

#if AF_ASIC_BM1397
// 9-byte frames: AA 55, nonce or register value (4), midstate number or chip
// address, job id or register, CRC with bit 7 set on a nonce
bool receive(AsicResult &out, int timeout_ms)
{
    uint8_t b[9];
    int n = uart_read_bytes(UART, b, sizeof(b), pdMS_TO_TICKS(timeout_ms));
    if (n <= 0) return false;
    if (n != 9 || b[0] != 0xAA || b[1] != 0x55) {
        uart_flush_input(UART);
        return false;
    }
    uint32_t nonce;
    memcpy(&nonce, b + 2, 4);
    if (!(b[8] & 0x80)) {
        out.is_register = true;
        out.data = __builtin_bswap32(nonce);
        out.reg = b[7];
        out.chip = s_addr_interval ? b[6] / s_addr_interval : 0;
        return true;
    }
    out.is_register = false;
    out.job_slot = b[7] & 0xfc;
    out.midstate = b[7] & 0x03;
    out.nonce = nonce;
    out.version_bits = 0;
    uint32_t nh = __builtin_bswap32(nonce);
    out.chip = s_nonce_interval ? ((nh >> 17) & 0xff) / s_nonce_interval : 0;
    return true;
}
#else
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
    out.midstate = 0;
    uint32_t nh = __builtin_bswap32(nonce);
    out.chip = s_nonce_interval ? ((nh >> 17) & 0xff) / s_nonce_interval : 0;
    return true;
}
#endif

void read_register_all(uint8_t reg) { send2(CMD_READ_ALL, 0x00, reg); }

void reset_register_all(uint8_t reg) { write_all(reg, 0, 0, 0, 0); }

} // namespace asic
