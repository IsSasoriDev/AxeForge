#include "weather.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "cJSON.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "core/settings.h"
#include "core/state.h"
#include "net/http_util.h"
#include "net/wifi.h"

static const char *TAG = "weather";

#define REFRESH_S (15 * 60)
#define RETRY_S 60
#define RESP_MAX 6144

static TaskHandle_t s_task;

static void on_time_sync(struct timeval *)
{
    g_state.time_synced = true;
    ESP_LOGI(TAG, "time synced");
}

static char *http_get(const char *url) { return http::get(url, RESP_MAX); }

static void url_encode(const char *in, char *out, size_t len)
{
    static const char *hex = "0123456789ABCDEF";
    size_t o = 0;
    for (; *in && o + 4 < len; in++) {
        unsigned char ch = (unsigned char) *in;
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '.') {
            out[o++] = ch;
        } else {
            out[o++] = '%';
            out[o++] = hex[ch >> 4];
            out[o++] = hex[ch & 15];
        }
    }
    out[o] = 0;
}

static double num(cJSON *obj, const char *key)
{
    cJSON *v = cJSON_GetObjectItem(obj, key);
    return cJSON_IsNumber(v) ? v->valuedouble : 0;
}

static bool geocode(Settings &cfg)
{
    char enc[144], url[256];
    url_encode(cfg.city, enc, sizeof(enc));
    snprintf(url, sizeof(url), "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=1&format=json", enc);
    char *body = http_get(url);
    if (!body) return false;
    bool ok = false;
    cJSON *j = cJSON_Parse(body);
    cJSON *r = cJSON_GetArrayItem(cJSON_GetObjectItem(j, "results"), 0);
    if (r && cJSON_IsNumber(cJSON_GetObjectItem(r, "latitude"))) {
        cfg.lat = (float) cJSON_GetObjectItem(r, "latitude")->valuedouble;
        cfg.lon = (float) cJSON_GetObjectItem(r, "longitude")->valuedouble;
        cJSON *name = cJSON_GetObjectItem(r, "name");
        if (cJSON_IsString(name)) strncpy(g_state.weather.city, name->valuestring, sizeof(g_state.weather.city) - 1);
        settings::save();
        ok = true;
        ESP_LOGI(TAG, "%s -> %.3f, %.3f", cfg.city, cfg.lat, cfg.lon);
    } else {
        snprintf(g_state.weather.error, sizeof(g_state.weather.error), "City \"%.30s\" not found", cfg.city);
    }
    cJSON_Delete(j);
    free(body);
    return ok;
}

static bool fetch(const Settings &cfg)
{
    char url[400];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,apparent_temperature,relative_humidity_2m,is_day,weather_code,wind_speed_10m"
             "&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=3%s",
             cfg.lat, cfg.lon, cfg.fahrenheit ? "&temperature_unit=fahrenheit&wind_speed_unit=mph" : "");
    char *body = http_get(url);
    if (!body) {
        snprintf(g_state.weather.error, sizeof(g_state.weather.error), "Weather service unreachable");
        return false;
    }
    cJSON *j = cJSON_Parse(body);
    free(body);
    cJSON *cur = cJSON_GetObjectItem(j, "current");
    cJSON *daily = cJSON_GetObjectItem(j, "daily");
    if (!cur || !daily) {
        cJSON_Delete(j);
        return false;
    }
    WeatherState &w = g_state.weather;
    w.temp = num(cur, "temperature_2m");
    w.feels = num(cur, "apparent_temperature");
    w.humidity = (uint8_t) num(cur, "relative_humidity_2m");
    w.wind = num(cur, "wind_speed_10m");
    w.code = (int8_t) num(cur, "weather_code");
    w.is_day = num(cur, "is_day") != 0;
    w.utc_offset_s = (int32_t) num(j, "utc_offset_seconds");
    cJSON *codes = cJSON_GetObjectItem(daily, "weather_code");
    cJSON *tmax = cJSON_GetObjectItem(daily, "temperature_2m_max");
    cJSON *tmin = cJSON_GetObjectItem(daily, "temperature_2m_min");
    for (int i = 0; i < 3; i++) {
        cJSON *c = cJSON_GetArrayItem(codes, i);
        w.days[i].code = c ? (int8_t) c->valueint : -1;
        cJSON *hi = cJSON_GetArrayItem(tmax, i), *lo = cJSON_GetArrayItem(tmin, i);
        w.days[i].tmax = hi ? (float) hi->valuedouble : 0;
        w.days[i].tmin = lo ? (float) lo->valuedouble : 0;
    }
    if (!w.city[0]) strncpy(w.city, cfg.city, sizeof(w.city) - 1);
    w.updated_s = g_state.uptime_s;
    w.valid = true;
    w.error[0] = 0;
    cJSON_Delete(j);
    ESP_LOGI(TAG, "%.1f, code %d, utc%+ld s", w.temp, w.code, (long) w.utc_offset_s);
    return true;
}

static void weather_task(void *)
{
    while (!wifi::connected()) vTaskDelay(pdMS_TO_TICKS(1000));

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    sntp_set_time_sync_notification_cb(on_time_sync);
    esp_sntp_init();

    bool force_geocode = false;
    while (true) {
        uint32_t wait_s = REFRESH_S;
        Settings &cfg = settings::get();
        if (!cfg.city[0]) {
            snprintf(g_state.weather.error, sizeof(g_state.weather.error), "Set your city in the web UI");
            g_state.weather.valid = false;
            wait_s = 3600;
        } else if (wifi::connected()) {
            bool located = cfg.lat != 0 || cfg.lon != 0;
            if (!located || force_geocode) located = geocode(cfg);
            if (!located || !fetch(cfg)) wait_s = RETRY_S;
        } else {
            wait_s = RETRY_S;
        }
        // a notification means settings changed: re-resolve the city
        force_geocode = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait_s * 1000)) > 0;
        if (force_geocode) {
            g_state.weather.city[0] = 0;
            g_state.weather.valid = false;
        }
    }
}

namespace weather {

void start() { xTaskCreate(weather_task, "weather", 6144, NULL, 3, &s_task); }

void refresh_now()
{
    if (s_task) xTaskNotifyGive(s_task);
}

bool local_time(struct tm *out)
{
    if (!g_state.time_synced) return false;
    time_t now = time(NULL) + g_state.weather.utc_offset_s;
    gmtime_r(&now, out);
    return true;
}

const char *describe(int c)
{
    if (c == 0) return "Clear sky";
    if (c == 1) return "Mainly clear";
    if (c == 2) return "Partly cloudy";
    if (c == 3) return "Overcast";
    if (c == 45 || c == 48) return "Fog";
    if (c >= 51 && c <= 57) return "Drizzle";
    if (c >= 61 && c <= 67) return "Rain";
    if (c >= 71 && c <= 77) return "Snow";
    if (c >= 80 && c <= 82) return "Showers";
    if (c == 85 || c == 86) return "Snow showers";
    if (c >= 95) return "Thunderstorm";
    return "--";
}

} // namespace weather
