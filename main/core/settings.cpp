#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "core/board_config.h"
#include "core/json_util.h"  // depth-limited parse for untrusted JSON
#include "core/state.h"

static const char *TAG = "settings";
static Settings s_settings;
// Guards the pool usernames, which the web server rewrites while the job task
// reads them for the payout check. Held only for the copy itself.
static portMUX_TYPE s_user_mux = portMUX_INITIALIZER_UNLOCKED;

static void get_str(nvs_handle_t h, const char *key, char *dst, size_t len)
{
    size_t n = len;
    nvs_get_str(h, key, dst, &n);  // keeps the default on error
}

static void get_u16(nvs_handle_t h, const char *key, uint16_t &dst)
{
    uint16_t v;
    if (nvs_get_u16(h, key, &v) == ESP_OK) dst = v;
}

static void get_float(nvs_handle_t h, const char *key, float &dst)
{
    uint32_t v;
    if (nvs_get_u32(h, key, &v) == ESP_OK) memcpy(&dst, &v, 4);
}

static void set_float(nvs_handle_t h, const char *key, float f)
{
    uint32_t v;
    memcpy(&v, &f, 4);
    nvs_set_u32(h, key, v);
}

static void get_bool(nvs_handle_t h, const char *key, bool &dst)
{
    uint8_t v;
    if (nvs_get_u8(h, key, &v) == ESP_OK) dst = v;
}

static void get_u8(nvs_handle_t h, const char *key, uint8_t &dst)
{
    uint8_t v;
    if (nvs_get_u8(h, key, &v) == ESP_OK) dst = v;
}

static void copy_json_str(cJSON *o, const char *key, char *dst, size_t len)
{
    cJSON *v = cJSON_GetObjectItem(o, key);
    if (!cJSON_IsString(v)) return;
    strncpy(dst, v->valuestring, len - 1);
    dst[len - 1] = 0;
}

namespace settings {

Settings &get() { return s_settings; }

void pool_user(int pool, char *out, size_t len)
{
    if (!len) return;
    portENTER_CRITICAL(&s_user_mux);
    strncpy(out, s_settings.pools[pool].user, len - 1);
    portEXIT_CRITICAL(&s_user_mux);
    out[len - 1] = 0;
}

char *pools_to_json()
{
    cJSON *a = cJSON_CreateArray();
    for (const PoolCfg &p : s_settings.pools) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "enabled", p.enabled);
        cJSON_AddStringToObject(o, "url", p.url);
        cJSON_AddNumberToObject(o, "port", p.port);
        cJSON_AddStringToObject(o, "user", p.user);
        cJSON_AddStringToObject(o, "pass", p.pass);
        cJSON_AddNumberToObject(o, "proto", (int) p.proto);
        cJSON_AddNumberToObject(o, "weight", p.weight);
        cJSON_AddStringToObject(o, "sv2key", p.sv2_key);
        cJSON_AddItemToArray(a, o);
    }
    char *out = cJSON_PrintUnformatted(a);
    cJSON_Delete(a);
    return out;
}

void pools_from_json(const char *json)
{
    cJSON *a = json_parse(json);
    if (!cJSON_IsArray(a)) {
        cJSON_Delete(a);
        return;
    }
    for (int i = 0; i < AF_MAX_POOLS; i++) {
        cJSON *o = cJSON_GetArrayItem(a, i);
        if (!o) break;
        PoolCfg &p = s_settings.pools[i];
        cJSON *v;
        if ((v = cJSON_GetObjectItem(o, "enabled"))) p.enabled = cJSON_IsTrue(v);
        copy_json_str(o, "url", p.url, sizeof(p.url));
        if (cJSON_IsNumber(v = cJSON_GetObjectItem(o, "port"))) p.port = (uint16_t) v->valueint;
        char user[sizeof(p.user)];
        strcpy(user, p.user);
        copy_json_str(o, "user", user, sizeof(user));
        portENTER_CRITICAL(&s_user_mux);
        memcpy(p.user, user, sizeof(user));
        portEXIT_CRITICAL(&s_user_mux);
        copy_json_str(o, "pass", p.pass, sizeof(p.pass));
        if (cJSON_IsNumber(v = cJSON_GetObjectItem(o, "proto")) && v->valueint >= 0 && v->valueint <= 2)
            p.proto = (PoolProto) v->valueint;
        if (cJSON_IsNumber(v = cJSON_GetObjectItem(o, "weight")))
            p.weight = (uint8_t) (v->valueint < 0 ? 0 : v->valueint > 100 ? 100 : v->valueint);
        copy_json_str(o, "sv2key", p.sv2_key, sizeof(p.sv2_key));
    }
    cJSON_Delete(a);
}

#if AF_KEEP_STOCK_NVS
// First boot after switching from AxeOS / the NerdQAxe stock firmware: take over its Wi-Fi, pools and
// clock settings (read-only, the "main" namespace is left untouched) so the
// miner comes back on the same network instead of opening a setup AP.
static bool import_axeos(Settings &s)
{
    nvs_handle_t h;
    if (nvs_open("main", NVS_READONLY, &h) != ESP_OK) return false;
    get_str(h, "wifissid", s.wifi_ssid, sizeof(s.wifi_ssid));
    get_str(h, "wifipass", s.wifi_pass, sizeof(s.wifi_pass));
    bool got_wifi = s.wifi_ssid[0] != 0;

    PoolCfg &p = s.pools[0];
    char url[sizeof(p.url)] = "";
    get_str(h, "stratumurl", url, sizeof(url));
    if (url[0]) {
        strcpy(p.url, url);
        p.enabled = true;
        p.proto = PoolProto::V1;
        get_u16(h, "stratumport", p.port);
        get_str(h, "stratumuser", p.user, sizeof(p.user));
        get_str(h, "stratumpass", p.pass, sizeof(p.pass));
    }
    PoolCfg &fb = s.pools[1];
    url[0] = 0;
    get_str(h, "fbstratumurl", url, sizeof(url));
    if (url[0]) {
        strcpy(fb.url, url);
        fb.enabled = true;
        fb.proto = PoolProto::V1;
        get_u16(h, "fbstratumport", fb.port);
        get_str(h, "fbstratumuser", fb.user, sizeof(fb.user));
        get_str(h, "fbstratumpass", fb.pass, sizeof(fb.pass));
        s.pool_mode = PoolMode::Failover;  // AxeOS treats it as a fallback, not a split
    }
    char f[16] = "";
    get_str(h, "asicfrequency_f", f, sizeof(f));  // AxeOS stores it as a string
    if (atof(f) > 0) s.freq_mhz = (uint16_t) atof(f);
    else get_u16(h, "asicfrequency", s.freq_mhz);  // ESP-Miner-NerdQAxePlus: a number
    get_u16(h, "asicvoltage", s.core_mv);
    nvs_close(h);
    ESP_LOGW(TAG, "imported AxeOS settings: wifi '%s', pool %s:%u, %u MHz @ %u mV", s.wifi_ssid, p.url, p.port,
             s.freq_mhz, s.core_mv);
    return got_wifi;
}
#endif

void load()
{
    nvs_handle_t h;
    Settings &s = s_settings;
    // sensible default so a fresh device has one pool slot ready to fill in
    strcpy(s.pools[0].url, "public-pool.io");
    s.pools[0].port = 21496;
    s.pools[0].enabled = true;
#if CONFIG_AF_BOARD_NERDAXE
    s.freq_mhz = 485;  // BM1366 stock point
    s.core_mv = 1200;
    s.display_type = 1;  // T-Display-S3
    s.rotate_180 = true; // mounted the other way round than on the NerdQaxe++
#endif
#if CONFIG_AF_BOARD_NERDQAXEPLUS || CONFIG_AF_BOARD_NERDOCTAXEPLUS
    s.freq_mhz = 490;  // BM1368 stock point
    s.core_mv = 1250;
#endif
#if CONFIG_AF_BOARD_NERDOCTAXEGAMMA
    s.freq_mhz = 600;
    s.core_mv = 1150;
#endif
#if CONFIG_AF_BOARD_NERDQX
    s.freq_mhz = 777;  // reference firmware default
    s.core_mv = 1200;
    s.rotate_180 = true;
#endif
#if CONFIG_AF_BOARD_BITAXE_SUPRA
    s.freq_mhz = 490;  // reference firmware defaults
    s.core_mv = 1166;
#endif
#if CONFIG_AF_BOARD_BITAXE_GAMMA
    s.freq_mhz = 525;
    s.core_mv = 1150;
#endif
#if CONFIG_AF_BOARD_NAJA
    s.freq_mhz = 327;
    s.core_mv = 1000;
    s.display_type = 1;  // ST7789 320x170, the T-Display-S3 screen
#endif
#if AF_HEX_FAMILY
#if CONFIG_AF_BOARD_ULTRAHEX
    s.freq_mhz = 485;  // BM1366 stock point
    s.core_mv = 1200;
#else
    s.freq_mhz = 490;  // BM1368 stock point
    s.core_mv = 1166;
#endif
    // two board sensors for six chips: keep real airflow over the unsensed ones
    s.fan_min_pct = 45;
#endif
#if CONFIG_AF_BOARD_NEXUS
    s.freq_mhz = 450;  // the Nexus S1's stock point
    s.core_mv = 950;
    s.display_type = 1;  // T-Display-S3 panel
#endif

    if (nvs_open("axeforge", NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGW(TAG, "no saved settings, using defaults");
#if AF_KEEP_STOCK_NVS
        if (import_axeos(s)) save();
#endif
        return;
    }
    get_str(h, "ssid", s.wifi_ssid, sizeof(s.wifi_ssid));
    get_str(h, "wpass", s.wifi_pass, sizeof(s.wifi_pass));
    get_str(h, "host", s.hostname, sizeof(s.hostname));

    size_t len = 0;
    if (nvs_get_str(h, "pools", NULL, &len) == ESP_OK && len > 1) {
        char *json = (char *) malloc(len);
        nvs_get_str(h, "pools", json, &len);
        pools_from_json(json);
        free(json);
    } else {
        // migrate the single-pool settings from earlier versions
        PoolCfg &p = s.pools[0];
        get_str(h, "purl", p.url, sizeof(p.url));
        get_u16(h, "pport", p.port);
        get_str(h, "puser", p.user, sizeof(p.user));
        get_str(h, "ppass", p.pass, sizeof(p.pass));
    }
    uint8_t mode = (uint8_t) s.pool_mode;
    get_u8(h, "pmode", mode);
    s.pool_mode = mode ? PoolMode::Failover : PoolMode::Split;

    get_u16(h, "freq", s.freq_mhz);
    get_u16(h, "mv", s.core_mv);
    get_bool(h, "atune", s.autotune);
    get_u16(h, "tfreq", s.tuned_freq);
    get_u16(h, "tmv", s.tuned_mv);
    get_u8(h, "tgoal", s.tune_goal);
    get_u16(h, "pcap", s.power_cap_w);
    get_bool(h, "chiptune", s.chip_tune);
    {
        size_t n = sizeof(s.chip_off);
        if (nvs_get_blob(h, "chipoff", s.chip_off, &n) != ESP_OK || n != sizeof(s.chip_off))
            memset(s.chip_off, 0, sizeof(s.chip_off));
    }
    get_u8(h, "fantgt", s.fan_target_c);
    get_u8(h, "fanmin", s.fan_min_pct);
    get_u8(h, "fmode0", s.fan_mode[0]);
    get_u8(h, "fmode1", s.fan_mode[1]);
    get_u8(h, "fman0", s.fan_manual[0]);
    get_u8(h, "fman1", s.fan_manual[1]);
    get_u8(h, "theme", s.theme);
    get_u8(h, "bright", s.brightness);
    get_u8(h, "scroff", s.screen_off_min);
    get_u8(h, "disptype", s.display_type);
    get_str(h, "pages", s.pages, sizeof(s.pages));
    get_u8(h, "defpage", s.default_page);
    get_u8(h, "rotate", s.page_rotate_s);
    get_bool(h, "rot180", s.rotate_180);
    get_str(h, "city", s.city, sizeof(s.city));
    get_float(h, "lat", s.lat);
    get_float(h, "lon", s.lon);
    get_bool(h, "fahr", s.fahrenheit);
    get_bool(h, "h24", s.clock_24h);
    get_bool(h, "mempool", s.mempool_on);
    get_str(h, "mpurl", s.mempool_url, sizeof(s.mempool_url));
    get_u8(h, "paymode", s.payout_mode);
    get_u8(h, "payfee", s.payout_max_fee);
    get_u16(h, "jobms", s.job_ms);
    nvs_get_u32(h, "vrhz", &s.vr_hz);
    get_str(h, "discord", s.discord_url, sizeof(s.discord_url));
    get_str(h, "tgtoken", s.tg_token, sizeof(s.tg_token));
    get_str(h, "tgchat", s.tg_chat, sizeof(s.tg_chat));
    get_u8(h, "notify", s.notify_events);
    get_bool(h, "bap", s.bap_on);
    get_bool(h, "blesetup", s.ble_setup);
    get_bool(h, "otaguard", s.ota_guard);
    get_bool(h, "priceon", s.price_on);
    get_u8(h, "pricesrc", s.price_src);
    get_str(h, "pricezone", s.price_zone, sizeof(s.price_zone));
    get_str(h, "priceurl", s.price_url, sizeof(s.price_url));
    get_str(h, "pricepath", s.price_path, sizeof(s.price_path));
    get_float(h, "pricemax", s.price_max);
    nvs_close(h);
}

void save()
{
    nvs_handle_t h;
    if (nvs_open("axeforge", NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed");
        return;
    }
    const Settings &s = s_settings;
    nvs_set_str(h, "ssid", s.wifi_ssid);
    nvs_set_str(h, "wpass", s.wifi_pass);
    nvs_set_str(h, "host", s.hostname);
    char *pools = pools_to_json();
    nvs_set_str(h, "pools", pools);
    free(pools);
    nvs_set_u8(h, "pmode", (uint8_t) s.pool_mode);
    nvs_set_u16(h, "freq", s.freq_mhz);
    nvs_set_u16(h, "mv", s.core_mv);
    nvs_set_u8(h, "atune", s.autotune);
    nvs_set_u16(h, "tfreq", s.tuned_freq);
    nvs_set_u16(h, "tmv", s.tuned_mv);
    nvs_set_u8(h, "tgoal", s.tune_goal);
    nvs_set_u16(h, "pcap", s.power_cap_w);
    nvs_set_u8(h, "chiptune", s.chip_tune);
    nvs_set_blob(h, "chipoff", s.chip_off, sizeof(s.chip_off));
    nvs_set_u8(h, "fantgt", s.fan_target_c);
    nvs_set_u8(h, "fanmin", s.fan_min_pct);
    nvs_set_u8(h, "fmode0", s.fan_mode[0]);
    nvs_set_u8(h, "fmode1", s.fan_mode[1]);
    nvs_set_u8(h, "fman0", s.fan_manual[0]);
    nvs_set_u8(h, "fman1", s.fan_manual[1]);
    nvs_set_u8(h, "theme", s.theme);
    nvs_set_u8(h, "bright", s.brightness);
    nvs_set_u8(h, "scroff", s.screen_off_min);
    nvs_set_u8(h, "disptype", s.display_type);
    nvs_set_str(h, "pages", s.pages);
    nvs_set_u8(h, "defpage", s.default_page);
    nvs_set_u8(h, "rotate", s.page_rotate_s);
    nvs_set_u8(h, "rot180", s.rotate_180);
    nvs_set_str(h, "city", s.city);
    set_float(h, "lat", s.lat);
    set_float(h, "lon", s.lon);
    nvs_set_u8(h, "fahr", s.fahrenheit);
    nvs_set_u8(h, "h24", s.clock_24h);
    nvs_set_u8(h, "mempool", s.mempool_on);
    nvs_set_str(h, "mpurl", s.mempool_url);
    nvs_set_u8(h, "paymode", s.payout_mode);
    nvs_set_u8(h, "payfee", s.payout_max_fee);
    nvs_set_u16(h, "jobms", s.job_ms);
    nvs_set_u32(h, "vrhz", s.vr_hz);
    nvs_set_str(h, "discord", s.discord_url);
    nvs_set_str(h, "tgtoken", s.tg_token);
    nvs_set_str(h, "tgchat", s.tg_chat);
    nvs_set_u8(h, "notify", s.notify_events);
    nvs_set_u8(h, "bap", s.bap_on);
    nvs_set_u8(h, "blesetup", s.ble_setup);
    nvs_set_u8(h, "otaguard", s.ota_guard);
    nvs_set_u8(h, "priceon", s.price_on);
    nvs_set_u8(h, "pricesrc", s.price_src);
    nvs_set_str(h, "pricezone", s.price_zone);
    nvs_set_str(h, "priceurl", s.price_url);
    nvs_set_str(h, "pricepath", s.price_path);
    set_float(h, "pricemax", s.price_max);
    nvs_commit(h);
    nvs_close(h);
}

bool valid_hostname(const char *h)
{
    size_t n = strlen(h);
    if (n < 1 || n > 32 || h[0] == '-' || h[n - 1] == '-') return false;
    for (size_t i = 0; i < n; i++) {
        char c = h[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-')) return false;
    }
    return true;
}

void worker_of(const char *user, char *out, size_t len)
{
    const char *dot = strchr(user, '.');
    snprintf(out, len, "%s", dot ? dot + 1 : "");
}

void own_worker(char *out, size_t len)
{
    const Settings &s = s_settings;
    int pick = -1;
    for (int i = 0; i < AF_MAX_POOLS && pick < 0; i++)
        if (s.pools[i].enabled && g_state.pools[i].connected) pick = i;
    for (int i = 0; i < AF_MAX_POOLS && pick < 0; i++)
        if (s.pools[i].enabled && s.pools[i].user[0]) pick = i;
    if (pick < 0) out[0] = 0;
    else worker_of(s.pools[pick].user, out, len);
}

} // namespace settings
