#include "stats.h"

#include <string.h>
#include <sys/time.h>
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"

#include "core/state.h"

static const char *TAG = "stats";

// ------------------------------------------------------------------ best shares
static stats::TopShare s_top[AF_TOP_SHARES];
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_dirty = false;

// ------------------------------------------------------------------ 24 h series
// One point per 5 minutes: the average of the one-minute samples in it.
#define DAY_STEP_S 300
#define DAY_POINTS (24 * 3600 / DAY_STEP_S)
static EXT_RAM_BSS_ATTR float s_hr[DAY_POINTS], s_temp[DAY_POINTS], s_power[DAY_POINTS];  // PSRAM
static int s_head = 0, s_count = 0;
static float s_acc_hr = 0, s_acc_temp = 0, s_acc_power = 0;
static int s_acc_n = 0;

namespace stats {

void load()
{
    nvs_handle_t h;
    if (nvs_open("axeforge", NVS_READONLY, &h) != ESP_OK) return;
    size_t len = sizeof(s_top);
    if (nvs_get_blob(h, "top", s_top, &len) != ESP_OK || len != sizeof(s_top)) memset(s_top, 0, sizeof(s_top));
    nvs_close(h);
    if (s_top[0].diff > g_state.best_diff) g_state.best_diff = s_top[0].diff;
}

void offer(double diff, int pool, int chip)
{
    if (diff <= s_top[AF_TOP_SHARES - 1].diff) return;  // the usual case, no lock needed
    time_t now = time(NULL);
    TopShare t;
    t.diff = diff;
    t.time = g_state.time_synced ? (uint32_t) now : 0;
    t.uptime = g_state.uptime_s;
    t.pool = (int8_t) pool;
    t.chip = (int8_t) chip;
    portENTER_CRITICAL(&s_mux);
    if (diff <= s_top[AF_TOP_SHARES - 1].diff) {  // lost a race with reset() or another offer
        portEXIT_CRITICAL(&s_mux);
        return;
    }
    int i = AF_TOP_SHARES - 1;
    while (i > 0 && s_top[i - 1].diff < diff) {
        s_top[i] = s_top[i - 1];
        i--;
    }
    s_top[i] = t;
    s_dirty = true;
    portEXIT_CRITICAL(&s_mux);
}

void reset()
{
    portENTER_CRITICAL(&s_mux);
    memset(s_top, 0, sizeof(s_top));
    s_dirty = true;
    portEXIT_CRITICAL(&s_mux);
    g_state.best_diff = g_state.session_best = 0;
    ESP_LOGW(TAG, "best shares cleared");
}

cJSON *scoreboard_json()
{
    TopShare copy[AF_TOP_SHARES];
    portENTER_CRITICAL(&s_mux);
    memcpy(copy, s_top, sizeof(copy));
    portEXIT_CRITICAL(&s_mux);
    cJSON *a = cJSON_CreateArray();
    for (const TopShare &t : copy) {
        if (t.diff <= 0) break;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "diff", t.diff);
        cJSON_AddNumberToObject(o, "time", t.time);
        cJSON_AddNumberToObject(o, "uptime", t.uptime);
        cJSON_AddNumberToObject(o, "pool", t.pool);
        cJSON_AddNumberToObject(o, "chip", t.chip);
        cJSON_AddItemToArray(a, o);
    }
    return a;
}

void tick()
{
    s_acc_hr += g_state.hashrate_ghs;
    s_acc_temp += g_state.chip_temp_max;
    s_acc_power += g_state.pin;
    if (++s_acc_n * 60 >= DAY_STEP_S) {
        s_hr[s_head] = s_acc_hr / s_acc_n;
        s_temp[s_head] = s_acc_temp / s_acc_n;
        s_power[s_head] = s_acc_power / s_acc_n;
        s_head = (s_head + 1) % DAY_POINTS;
        if (s_count < DAY_POINTS) s_count++;
        s_acc_hr = s_acc_temp = s_acc_power = 0;
        s_acc_n = 0;
    }

    if (!s_dirty) return;
    TopShare copy[AF_TOP_SHARES];
    portENTER_CRITICAL(&s_mux);
    memcpy(copy, s_top, sizeof(copy));
    s_dirty = false;
    portEXIT_CRITICAL(&s_mux);
    nvs_handle_t h;
    if (nvs_open("axeforge", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "top", copy, sizeof(copy));
    nvs_commit(h);
    nvs_close(h);
}

cJSON *day_json()
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "step", DAY_STEP_S);
    cJSON *hr = cJSON_AddArrayToObject(r, "hashrate");
    cJSON *tp = cJSON_AddArrayToObject(r, "temp");
    cJSON *pw = cJSON_AddArrayToObject(r, "power");
    int start = (s_head - s_count + DAY_POINTS) % DAY_POINTS;
    for (int i = 0; i < s_count; i++) {
        int k = (start + i) % DAY_POINTS;
        cJSON_AddItemToArray(hr, cJSON_CreateNumber((int) s_hr[k]));
        cJSON_AddItemToArray(tp, cJSON_CreateNumber((int) (s_temp[k] * 10) / 10.0));
        cJSON_AddItemToArray(pw, cJSON_CreateNumber((int) (s_power[k] * 10) / 10.0));
    }
    return r;
}

}
