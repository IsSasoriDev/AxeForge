#include "bap.h"

#include "core/board_config.h"

#if (AF_BITAXE_FAMILY || AF_HEX_FAMILY) && !CONFIG_AF_BOARD_NAJA  // the Naja's color board has no BAP header

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/uart.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "asic/bm13xx.h"
#include "core/board.h"
#include "core/settings.h"
#include "core/state.h"

static const char *TAG = "bap";

#define BAP_UART UART_NUM_2
#define PIN_TX 39
#define PIN_RX 40
#define BAP_LINE_MAX 256
#define SUB_TIMEOUT_S 300  // as AxeOS: a subscription lapses unless renewed

enum Param { HASHRATE, TEMPERATURE, POWER, VOLTAGE, CURRENT, SHARES, FAN_SPEED, BEST_DIFF, BLOCK_HEIGHT, WIFI, FOUND_BLOCK, N_PARAMS };
static const char *const PARAM_NAMES[N_PARAMS] = {"hashrate",        "temperature",  "power", "voltage",
                                                  "current",         "shares",       "fan_speed",
                                                  "best_difficulty", "block_height", "wifi",  "found_block"};
struct Sub {
    bool on;
    uint32_t interval_ms;
    uint32_t last_ms;
    uint32_t since_s;
};
static Sub s_subs[N_PARAMS];

static uint32_t now_ms() { return (uint32_t) (xTaskGetTickCount() * portTICK_PERIOD_MS); }

static void send(const char *cmd, const char *param, const char *value)
{
    char body[BAP_LINE_MAX - 8], line[BAP_LINE_MAX];
    snprintf(body, sizeof(body), "BAP,%s,%s%s%s", cmd, param, value ? "," : "", value ? value : "");
    uint8_t ck = 0;
    for (const char *p = body; *p; p++) ck ^= (uint8_t) *p;
    int n = snprintf(line, sizeof(line), "$%s*%02X\r\n", body, ck);
    uart_write_bytes(BAP_UART, line, n);
}

static void fmt_diff(double d, char *out, size_t len)
{
    static const char *const U[] = {"", "K", "M", "G", "T", "P", "E"};
    int i = 0;
    while (d >= 1000 && i < 6) {
        d /= 1000;
        i++;
    }
    snprintf(out, len, i ? "%.2f%s" : "%.0f%s", d, U[i]);
}

static void report(int p)
{
    const MinerState &s = g_state;
    char v[48];
    switch (p) {
    case HASHRATE: snprintf(v, sizeof(v), "%.2f", s.hashrate_ghs); send("RES", "hashrate", v); break;
    case TEMPERATURE:
        snprintf(v, sizeof(v), "%.1f", s.chip_temp_max);
        send("RES", "chipTemp", v);
        snprintf(v, sizeof(v), "%.1f", s.vr_temp);
        send("RES", "vrTemp", v);
        break;
    case POWER: snprintf(v, sizeof(v), "%.2f", s.pin); send("RES", "power", v); break;
    case VOLTAGE: snprintf(v, sizeof(v), "%.2f", s.vin); send("RES", "voltage", v); break;
    case CURRENT: snprintf(v, sizeof(v), "%.2f", s.iin); send("RES", "current", v); break;
    case SHARES:
        snprintf(v, sizeof(v), "%lu/%lu", (unsigned long) s.shares_accepted, (unsigned long) s.shares_rejected);
        send("RES", "shares", v);
        break;
    case FAN_SPEED: snprintf(v, sizeof(v), "%u", s.fan_rpm[0]); send("RES", "fan_speed", v); break;
    case BEST_DIFF: fmt_diff(s.best_diff, v, sizeof(v)); send("RES", "best_difficulty", v); break;
    case BLOCK_HEIGHT: snprintf(v, sizeof(v), "%lu", (unsigned long) s.block_height); send("RES", "block_height", v); break;
    case WIFI: {
        wifi_ap_record_t ap;
        int rssi = esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : -128;
        send("RES", "wifi_ssid", s.ssid);
        snprintf(v, sizeof(v), "%d", rssi);
        send("RES", "wifi_rssi", v);
        send("RES", "wifi_ip", s.ip);
        break;
    }
    case FOUND_BLOCK: snprintf(v, sizeof(v), "%lu", (unsigned long) s.blocks_found); send("RES", "block_found", v); break;
    }
}

static int param_of(const char *name)
{
    for (int i = 0; i < N_PARAMS; i++)
        if (!strcmp(name, PARAM_NAMES[i])) return i;
    return -1;
}

static void restart_soon()
{
    send("STA", "status", "restarting");
    vTaskDelay(pdMS_TO_TICKS(500));
    board::shutdown();
    esp_restart();
}

static void handle_set(const char *param, const char *value)
{
    Settings &c = settings::get();
    if (!value || !*value) {
        send("ERR", param, "missing_parameter");
        return;
    }
    if (!strcmp(param, "frequency")) {
        int f = atoi(value);
        if (f < AF_MIN_FREQ_MHZ || f > board::limits().max_freq_mhz) return send("ERR", param, "invalid_range");
        c.freq_mhz = board::clamp_freq(f);
        c.autotune = false;  // a fixed clock from the accessory, the tuner applies it
    } else if (!strcmp(param, "asic_voltage")) {
        int mv = atoi(value);
        if (mv < CONFIG_AF_MIN_CORE_MV || mv > board::max_mv()) return send("ERR", param, "invalid_range");
        c.core_mv = board::clamp_mv(mv);
        c.autotune = false;
    } else if (!strcmp(param, "fan_speed")) {
        int pct = atoi(value);
        if (pct < 20 || pct > 100) return send("ERR", param, "invalid_range");
        c.fan_mode[0] = c.fan_mode[1] = 1;
        c.fan_manual[0] = c.fan_manual[1] = (uint8_t) pct;
    } else if (!strcmp(param, "auto_fan")) {
        int on = atoi(value);
        if (on != 0 && on != 1) return send("ERR", param, "invalid_range");
        c.fan_mode[0] = c.fan_mode[1] = on ? 0 : 1;
    } else if (!strcmp(param, "ssid")) {
        if (strlen(value) >= sizeof(c.wifi_ssid)) return send("ERR", param, "invalid_range");
        snprintf(c.wifi_ssid, sizeof(c.wifi_ssid), "%s", value);
        settings::save();
        send("ACK", param, value);
        return;  // the password comes next and restarts
    } else if (!strcmp(param, "password")) {
        if (strlen(value) >= sizeof(c.wifi_pass)) return send("ERR", param, "invalid_range");
        snprintf(c.wifi_pass, sizeof(c.wifi_pass), "%s", value);
        settings::save();
        send("ACK", param, "password_set");
        restart_soon();
        return;
    } else {
        send("ERR", param, "unsupported_parameter");
        return;
    }
    settings::save();
    ESP_LOGI(TAG, "accessory set %s = %s", param, value);
    send("ACK", param, value);
}

static void handle(char *line)
{
    // $BAP,CMD,param[,value]*CK
    char *star = strrchr(line, '*');
    if (line[0] != '$' || !star || strlen(star) < 3) return;
    uint8_t ck = 0;
    for (char *p = line + 1; p < star; p++) ck ^= (uint8_t) *p;
    if (strtoul(star + 1, NULL, 16) != ck) {
        send("ERR", "checksum", "invalid_checksum");
        return;
    }
    *star = 0;
    char *save = NULL;
    char *talker = strtok_r(line + 1, ",", &save);
    char *cmd = strtok_r(NULL, ",", &save);
    char *param = strtok_r(NULL, ",", &save);
    char *value = strtok_r(NULL, "", &save);  // the rest, commas included
    if (!talker || strcmp(talker, "BAP") || !cmd) return;
    if (!param) {
        send("ERR", "unknown", "missing_parameter");
        return;
    }
    bool setup = g_state.status == SysStatus::SetupAP;
    if (!strcmp(cmd, "SET")) {
        if (setup && strcmp(param, "ssid") && strcmp(param, "password")) return send("ERR", param, "ap_mode_limited_settings");
        handle_set(param, value);
    } else if (!strcmp(cmd, "REQ")) {
        if (setup) return send("ERR", param, "ap_mode_no_requests");
        if (!strcmp(param, "systemInfo")) {
            char port[8];
            const PoolCfg &p = settings::get().pools[0];
            snprintf(port, sizeof(port), "%u", p.port);
            send("RES", "deviceModel", AF_BOARD_NAME);
            send("RES", "asicModel", asic::MODEL);
            send("RES", "firmware", esp_app_get_description()->version);
            send("RES", "pool", p.url);
            send("RES", "poolPort", port);
            char user[96];
            settings::pool_user(0, user, sizeof(user));
            send("RES", "poolUser", user);
            return;
        }
        int i = param_of(param);
        if (i < 0) return send("ERR", param, "unsupported_parameter");
        report(i);
    } else if (!strcmp(cmd, "SUB")) {
        if (setup) return send("ERR", param, "ap_mode_no_subscriptions");
        int i = param_of(param);
        if (i < 0) return send("ERR", param, "unsupported_parameter");
        int ms = value ? atoi(value) : 0;
        s_subs[i] = {true, (uint32_t) (ms < 1000 ? 1000 : ms > 60000 ? 60000 : ms), 0, g_state.uptime_s};
        send("ACK", param, "subscribed");
    } else if (!strcmp(cmd, "UNSUB")) {
        int i = param_of(param);
        if (i >= 0) s_subs[i].on = false;
        send("ACK", param, "unsubscribed");
    } else {
        send("ERR", cmd, "unsupported_command");
    }
}

static void task(void *)
{
    char line[BAP_LINE_MAX];
    int len = 0;
    uint8_t buf[64];
    send("STA", "status", "ready");
    while (true) {
        int n = uart_read_bytes(BAP_UART, buf, sizeof(buf), pdMS_TO_TICKS(100));
        for (int i = 0; i < n; i++) {
            char c = (char) buf[i];
            if (c == '$') len = 0;  // a new sentence always starts over
            if (c == '\r' || c == '\n') {
                if (len) {
                    line[len] = 0;
                    handle(line);
                }
                len = 0;
            } else if (len < BAP_LINE_MAX - 1) {
                line[len++] = c;
            } else {
                len = 0;  // too long: drop it
            }
        }
        uint32_t t = now_ms();
        for (int p = 0; p < N_PARAMS; p++) {
            Sub &s = s_subs[p];
            if (!s.on) continue;
            if (g_state.uptime_s - s.since_s > SUB_TIMEOUT_S) {
                s.on = false;
                send("STA", PARAM_NAMES[p], "subscription_timeout");
                continue;
            }
            if (t - s.last_ms >= s.interval_ms) {
                s.last_ms = t;
                report(p);
            }
        }
    }
}

namespace bap {

void start()
{
    if (!settings::get().bap_on) return;
    uart_config_t cfg = {};
    cfg.baud_rate = 115200;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;
    if (uart_driver_install(BAP_UART, 1024, 1024, 0, NULL, 0) != ESP_OK) return;
    uart_param_config(BAP_UART, &cfg);
    uart_set_pin(BAP_UART, PIN_TX, PIN_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    xTaskCreate(task, "bap", 4096, NULL, 3, NULL);
    ESP_LOGI(TAG, "accessory port on TX %d / RX %d", PIN_TX, PIN_RX);
}

}

#else
namespace bap {
void start() {}
}
#endif
