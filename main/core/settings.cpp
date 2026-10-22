#include "settings.h"

#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

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

void load()
{
    nvs_handle_t h;
    Settings &s = s_settings;
    // sensible default so a fresh device has one pool slot ready to fill in
    strcpy(s.pools[0].url, "public-pool.io");
    s.pools[0].port = 21496;
    s.pools[0].enabled = true;

    if (nvs_open("axeforge", NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGW(TAG, "no saved settings, using defaults");
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
