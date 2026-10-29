#include "price.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

#include "core/json_util.h"  // depth-limited parse for untrusted JSON
#include "core/settings.h"
#include "core/state.h"
#include "net/http_util.h"
#include "net/wifi.h"

static const char *TAG = "price";

#define SRC_ENERGY_CHARTS 0
#define SRC_URL 1
#define EC_REFRESH_S 3600     // day-ahead prices change once a day; the API rate-limits
#define URL_REFRESH_S 300
#define RETRY_S 600
#define URL_STALE_S 1800      // a URL price older than this no longer counts
#define MIN_SWITCH_S 600      // at least this long between stopping and starting
#define SLOTS_MAX 200         // two days of 15-minute prices
#define BODY_MAX (24 * 1024)

static TaskHandle_t s_task;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
EXT_RAM_BSS_ATTR static float s_slot[SLOTS_MAX];  // ct/kWh
static uint32_t s_first = 0, s_step = 3600;
static int s_n = 0;
static float s_now = NAN;          // current price, in the limit's unit
static uint32_t s_fetched_s = 0;   // uptime of the last good fetch
static bool s_have = false;
static volatile bool s_pause = false;
static char s_err[64] = "";

static bool valid_zone(const char *z)
{
    if (!*z) return false;
    for (; *z; z++)
        if (!((*z >= 'A' && *z <= 'Z') || (*z >= '0' && *z <= '9') || *z == '-' || *z == '_')) return false;
    return true;
}

static bool fetch_energy_charts(const char *zone)
{
    if (!valid_zone(zone)) {
        snprintf(s_err, sizeof(s_err), "Unknown bidding zone");
        return false;
    }
    time_t now = time(NULL);
    time_t a = now - 3600, b = now + 86400;
    struct tm ta, tb;
    gmtime_r(&a, &ta);
    gmtime_r(&b, &tb);
    char url[160];
    snprintf(url, sizeof(url), "https://api.energy-charts.info/price?bzn=%s&start=%04d-%02d-%02d&end=%04d-%02d-%02d", zone,
             ta.tm_year + 1900, ta.tm_mon + 1, ta.tm_mday, tb.tm_year + 1900, tb.tm_mon + 1, tb.tm_mday);
    char *body = http::get(url, BODY_MAX);
    if (!body) {
        snprintf(s_err, sizeof(s_err), "Energy-Charts didn't answer");
        return false;
    }
    cJSON *j = json_parse(body);
    free(body);
    cJSON *ts = cJSON_GetObjectItem(j, "unix_seconds");
    cJSON *ps = cJSON_GetObjectItem(j, "price");
    int n = cJSON_GetArraySize(ts);
    if (!cJSON_IsArray(ts) || !cJSON_IsArray(ps) || n < 1 || cJSON_GetArraySize(ps) != n) {
        cJSON_Delete(j);
        snprintf(s_err, sizeof(s_err), "No prices for %s", zone);
        return false;
    }
    if (n > SLOTS_MAX) n = SLOTS_MAX;
    uint32_t first = (uint32_t) cJSON_GetArrayItem(ts, 0)->valuedouble;
    uint32_t step = n > 1 ? (uint32_t) cJSON_GetArrayItem(ts, 1)->valuedouble - first : 3600;
    if (step < 300 || step > 3600) step = 3600;
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < n; i++) {
        cJSON *p = cJSON_GetArrayItem(ps, i);
        s_slot[i] = cJSON_IsNumber(p) ? (float) (p->valuedouble / 10.0) : NAN;  // EUR/MWh -> ct/kWh
    }
    s_first = first;
    s_step = step;
    s_n = n;
    portEXIT_CRITICAL(&s_mux);
    cJSON_Delete(j);
    ESP_LOGI(TAG, "%d prices for %s, every %lu min", n, zone, (unsigned long) (step / 60));
    return true;
}

// "a.b.0.c": object keys and array indexes
static cJSON *walk(cJSON *o, const char *path)
{
    char key[40];
    while (o && *path) {
        size_t k = strcspn(path, ".");
        if (k >= sizeof(key)) return NULL;
        memcpy(key, path, k);
        key[k] = 0;
        path += k + (path[k] == '.');
        if (cJSON_IsArray(o)) {
            char *end;
            long i = strtol(key, &end, 10);
            o = *end ? NULL : cJSON_GetArrayItem(o, (int) i);
        } else {
            o = cJSON_GetObjectItem(o, key);
        }
    }
    return o;
}

static bool fetch_url(const char *url, const char *path)
{
    char *body = http::get(url, BODY_MAX);
    if (!body) {
        snprintf(s_err, sizeof(s_err), "The price URL didn't answer");
        return false;
    }
    cJSON *j = json_parse(body);
    free(body);
    cJSON *v = walk(j, path);
    float p = NAN;
    if (cJSON_IsNumber(v)) p = (float) v->valuedouble;
    else if (cJSON_IsString(v)) {
        char *end;
        p = strtof(v->valuestring, &end);
        if (end == v->valuestring) p = NAN;
    }
    cJSON_Delete(j);
    if (isnan(p)) {
        snprintf(s_err, sizeof(s_err), "No number at \"%.40s\"", path);
        return false;
    }
    s_now = p;
    return true;
}

// The Energy-Charts price for this moment, NAN if the data doesn't cover it.
static float slot_now(int *index)
{
    uint32_t now = (uint32_t) time(NULL);
    float p = NAN;
    int i = -1;
    portENTER_CRITICAL(&s_mux);
    if (s_n && now >= s_first) {
        i = (int) ((now - s_first) / s_step);
        if (i < s_n) p = s_slot[i];
        else i = -1;
    }
    portEXIT_CRITICAL(&s_mux);
    if (index) *index = i;
    return p;
}

static void price_task(void *)
{
    uint32_t next_fetch = 0, switched_s = 0;
    uint8_t last_src = 0xff;
    char last_zone[sizeof(Settings::price_zone)] = "";
    char last_url[sizeof(Settings::price_url)] = "", last_path[sizeof(Settings::price_path)] = "";
    bool was_on = false;
    while (true) {
        const Settings &cfg = settings::get();
        uint32_t up = g_state.uptime_s;
        if (!cfg.price_on) {
            was_on = false;
            s_pause = false;
            s_have = false;
            s_now = NAN;
            s_err[0] = 0;
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(30000));
            continue;
        }
        // turned on, or a new source: fetch now, and the first stop needn't wait
        if (!was_on || cfg.price_src != last_src || strcmp(cfg.price_zone, last_zone) != 0 ||
            strcmp(cfg.price_url, last_url) != 0 || strcmp(cfg.price_path, last_path) != 0) {
            was_on = true;
            last_src = cfg.price_src;
            snprintf(last_zone, sizeof(last_zone), "%s", cfg.price_zone);
            snprintf(last_url, sizeof(last_url), "%s", cfg.price_url);
            snprintf(last_path, sizeof(last_path), "%s", cfg.price_path);
            s_have = false;
            s_n = 0;
            s_now = NAN;
            next_fetch = 0;
            switched_s = 0;
        }
        bool ec = cfg.price_src == SRC_ENERGY_CHARTS;
        if (!wifi::connected() || (ec && !g_state.time_synced)) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10000));
            continue;
        }
        int idx;
        bool ran_out = ec && s_have && (slot_now(&idx), idx < 0);
        if (!s_have || up >= next_fetch || ran_out) {
            bool ok = ec ? fetch_energy_charts(cfg.price_zone) : fetch_url(cfg.price_url, cfg.price_path);
            if (ok) {
                s_have = true;
                s_fetched_s = up;
                s_err[0] = 0;
                next_fetch = up + (ec ? EC_REFRESH_S : URL_REFRESH_S);
            } else {
                ESP_LOGW(TAG, "%s", s_err);
                next_fetch = up + (ec ? RETRY_S : URL_REFRESH_S);
            }
        }
        if (ec) s_now = slot_now(NULL);
        else if (s_have && up - s_fetched_s > URL_STALE_S) s_now = NAN;

        // no price means mining: a dead price feed must never stop the miner for good
        bool want = !isnan(s_now) && s_now > cfg.price_max;
        if (want != s_pause && (!switched_s || up - switched_s >= MIN_SWITCH_S)) {
            s_pause = want;
            switched_s = up;
            ESP_LOGW(TAG, "price %.2f, limit %.2f: %s mining", s_now, cfg.price_max, want ? "stopping" : "resuming");
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(60000));
    }
}

namespace price {

void start()
{
    // PSRAM stack: this task only does HTTP, never flash writes
    xTaskCreatePinnedToCoreWithCaps(price_task, "price", 8192, NULL, 2, &s_task, tskNO_AFFINITY, MALLOC_CAP_SPIRAM);
}

void refresh_now()
{
    if (s_task) xTaskNotifyGive(s_task);
}

bool want_pause() { return s_pause; }

cJSON *json()
{
    const Settings &cfg = settings::get();
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "on", cfg.price_on);
    cJSON_AddNumberToObject(r, "src", cfg.price_src);
    if (!isnan(s_now)) cJSON_AddNumberToObject(r, "now", round((double) s_now * 1000) / 1000);
    cJSON_AddNumberToObject(r, "max", cfg.price_max);
    cJSON_AddStringToObject(r, "unit", cfg.price_src == SRC_ENERGY_CHARTS ? "ct/kWh" : "");
    cJSON_AddBoolToObject(r, "pausing", s_pause);
    cJSON_AddNumberToObject(r, "age", s_have ? (double) (g_state.uptime_s - s_fetched_s) : -1.0);
    cJSON_AddStringToObject(r, "error", s_err);
    if (cfg.price_src == SRC_ENERGY_CHARTS && cfg.price_on) {
        int i;
        slot_now(&i);
        cJSON_AddNumberToObject(r, "step", s_step);
        cJSON *a = cJSON_AddArrayToObject(r, "slots");
        if (i >= 0) {
            float slots[96];
            int m = 0;
            portENTER_CRITICAL(&s_mux);  // one consistent copy: the price task may be refreshing
            uint32_t first = s_first, step = s_step;
            for (int k = i; k < s_n && m < 96; k++) slots[m++] = s_slot[k];
            portEXIT_CRITICAL(&s_mux);
            cJSON_AddNumberToObject(r, "start", (double) first + (double) i * step);
            for (int k = 0; k < m; k++) {
                float p = slots[k];
                cJSON_AddItemToArray(a, isnan(p) ? cJSON_CreateNull() : cJSON_CreateNumber(round((double) p * 100) / 100));
            }
        }
    }
    return r;
}

}
