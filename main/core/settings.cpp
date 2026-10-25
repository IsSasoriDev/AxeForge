#include "settings.h"

#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "settings";
static Settings s_settings;

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
    cJSON *a = cJSON_Parse(json);
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
        copy_json_str(o, "user", p.user, sizeof(p.user));
        copy_json_str(o, "pass", p.pass, sizeof(p.pass));
        if (cJSON_IsNumber(v = cJSON_GetObjectItem(o, "proto")) && v->valueint >= 0 && v->valueint <= 2)
            p.proto = (PoolProto) v->valueint;
        if (cJSON_IsNumber(v = cJSON_GetObjectItem(o, "weight")))
            p.weight = (uint8_t) (v->valueint < 0 ? 0 : v->valueint > 100 ? 100 : v->valueint);
        copy_json_str(o, "sv2key", p.sv2_key, sizeof(p.sv2_key));
    }
    cJSON_Delete(a);
}

#if CONFIG_AF_BOARD_SUPRAHEX
// First boot after switching from AxeOS: take over its Wi-Fi, pools and
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
    get_str(h, "asicfrequency_f", f, sizeof(f));
    if (atof(f) > 0) s.freq_mhz = (uint16_t) atof(f);
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
#if CONFIG_AF_BOARD_SUPRAHEX
    s.freq_mhz = 490;  // BM1368 stock point
    s.core_mv = 1166;
    // two board sensors for six chips: keep real airflow over the unsensed ones
    s.fan_min_pct = 45;
#endif

    if (nvs_open("axeforge", NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGW(TAG, "no saved settings, using defaults");
#if CONFIG_AF_BOARD_SUPRAHEX
        if (import_axeos(s)) save();
#endif
        return;
    }
    get_str(h, "ssid", s.wifi_ssid, sizeof(s.wifi_ssid));
    get_str(h, "wpass", s.wifi_pass, sizeof(s.wifi_pass));

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
    get_u8(h, "fantgt", s.fan_target_c);
    get_u8(h, "fanmin", s.fan_min_pct);
    get_u8(h, "fmode0", s.fan_mode[0]);
    get_u8(h, "fmode1", s.fan_mode[1]);
    get_u8(h, "fman0", s.fan_manual[0]);
    get_u8(h, "fman1", s.fan_manual[1]);
    get_u8(h, "theme", s.theme);
    get_u8(h, "bright", s.brightness);
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
    char *pools = pools_to_json();
    nvs_set_str(h, "pools", pools);
    free(pools);
    nvs_set_u8(h, "pmode", (uint8_t) s.pool_mode);
    nvs_set_u16(h, "freq", s.freq_mhz);
    nvs_set_u16(h, "mv", s.core_mv);
    nvs_set_u8(h, "atune", s.autotune);
    nvs_set_u16(h, "tfreq", s.tuned_freq);
    nvs_set_u16(h, "tmv", s.tuned_mv);
    nvs_set_u8(h, "fantgt", s.fan_target_c);
    nvs_set_u8(h, "fanmin", s.fan_min_pct);
    nvs_set_u8(h, "fmode0", s.fan_mode[0]);
    nvs_set_u8(h, "fmode1", s.fan_mode[1]);
    nvs_set_u8(h, "fman0", s.fan_manual[0]);
    nvs_set_u8(h, "fman1", s.fan_manual[1]);
    nvs_set_u8(h, "theme", s.theme);
    nvs_set_u8(h, "bright", s.brightness);
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
    nvs_commit(h);
    nvs_close(h);
}

} // namespace settings
