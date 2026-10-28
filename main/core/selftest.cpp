#include "selftest.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

#include "asic/bm13xx.h"
#include "core/board.h"
#include "core/state.h"

static const char *TAG = "selftest";

#define MEASURE_S 60
#define MIN_HASH_PCT 85.0f   // of the theoretical rate, as AxeOS's self-test
#define MAX_ERROR_PCT 2.0f

enum class St : uint8_t { Wait, Run, Pass, Warn, Fail };
struct Step {
    const char *name;
    St st;
    char detail[72];
};
static Step s_steps[] = {
    {"Temperature sensors", St::Wait, ""}, {"Voltage regulator", St::Wait, ""}, {"ASIC chips", St::Wait, ""},
    {"Fans", St::Wait, ""},                {"Hashrate", St::Wait, ""},          {"Error rate", St::Wait, ""},
    {"Power draw", St::Wait, ""},          {"Pool", St::Wait, ""},
};
#define N_STEPS (sizeof(s_steps) / sizeof(s_steps[0]))
static volatile bool s_running = false, s_done = false;

static void set(int i, St st, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void set(int i, St st, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_steps[i].detail, sizeof(s_steps[i].detail), fmt, ap);
    va_end(ap);
    s_steps[i].st = st;
    if (st == St::Fail || st == St::Warn)
        ESP_LOGW(TAG, "%s: %s", s_steps[i].name, s_steps[i].detail);
}

static void task(void *)
{
    const MinerState &s = g_state;
    // 1 sensors
    set(0, St::Run, "reading");
    if (!s.temp_ok) set(0, St::Fail, "a chip-side sensor doesn't answer");
    else if (!AF_HAS_VR_SENSOR) set(0, St::Pass, "chips %.0f °C", s.chip_temp_max);
    else if (s.vr_temp_ok) set(0, St::Pass, "chips %.0f °C, regulator %.0f °C", s.chip_temp_max, s.vr_temp);
    else set(0, St::Fail, "the regulator temperature doesn't answer");

    // 2 regulator
    set(1, St::Run, "checking");
    float set_v = board::core_mv() / 1000.0f;
    if (!board::is_powered()) set(1, St::Fail, "core power is off (%s)", s.fault[0] ? s.fault : "not started");
    else if (isnan(s.vout) || s.vout <= 0) set(1, St::Fail, "no output voltage reading");
    else if (fabsf(s.vout - set_v) > 0.15f * set_v) set(1, St::Fail, "%.3f V, set to %.3f V", s.vout, set_v);
    else set(1, St::Pass, "%.3f V (set %.3f V), %.1f A, input %.2f V", s.vout, set_v, s.iout, s.vin);

    // 3 chips
    if (s.chips_detected == AF_NUM_ASICS) set(2, St::Pass, "%d of %d %s answered", s.chips_detected, AF_NUM_ASICS, asic::MODEL);
    else set(2, St::Fail, "%d of %d %s answered", s.chips_detected, AF_NUM_ASICS, asic::MODEL);

    // 4 fans: a connected header must be turning
    int present = 0, spinning = 0;
    char rpm[40] = "";
    for (int i = 0; i < 2; i++) {
        if (!s.fan_present[i]) continue;
        present++;
        if (s.fan_rpm[i] > 300) spinning++;
        size_t o = strlen(rpm);
        snprintf(rpm + o, sizeof(rpm) - o, "%s%u rpm", o ? ", " : "", s.fan_rpm[i]);
    }
    if (!present) set(3, St::Fail, "no fan has been seen spinning");
    else if (spinning < present) set(3, St::Fail, "a connected fan stopped (%s)", rpm);
    else set(3, St::Pass, "%d turning: %s", present, rpm);

    // 5-7 one minute of mining
    set(4, St::Run, "measuring for %d s", MEASURE_S);
    set(5, St::Run, "measuring");
    set(6, St::Run, "measuring");
    float hr = 0, err = 0, pw = 0, exp_ghs = 0;
    int n = 0;
    for (int t = 0; t < MEASURE_S; t++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        hr += s.hashrate_ghs;
        err += s.error_pct;
        pw += s.pin;
        exp_ghs += s.expected_ghs;
        n++;
        snprintf(s_steps[4].detail, sizeof(s_steps[4].detail), "measuring, %d s left", MEASURE_S - t - 1);
    }
    hr /= n, err /= n, pw /= n, exp_ghs /= n;
    float pct = exp_ghs > 0 ? 100.0f * hr / exp_ghs : 0;
    if (exp_ghs <= 0) set(4, St::Fail, "not mining");
    else set(4, pct >= MIN_HASH_PCT ? St::Pass : St::Fail, "%.0f GH/s, %.0f%% of %.0f GH/s theoretical", hr, pct, exp_ghs);
    set(5, err <= MAX_ERROR_PCT ? St::Pass : St::Fail, "%.2f%% hardware errors (limit %.0f%%)", err, MAX_ERROR_PCT);
    float maxw = board::limits().max_power_w;
    set(6, pw <= maxw ? St::Pass : St::Fail, "%.1f W of %.0f W allowed", pw, maxw);

    // 8 pool: a warning only, the hardware can be fine without one
    if (s.pool_connected) set(7, St::Pass, "connected, %lu accepted / %lu rejected", (unsigned long) s.shares_accepted,
                              (unsigned long) s.shares_rejected);
    else set(7, St::Warn, "no pool connected: hardware checked without live work");

    bool ok = true;
    for (const Step &st : s_steps) ok &= st.st != St::Fail;
    ESP_LOGW(TAG, "self-test %s", ok ? "passed" : "FAILED");
    s_done = true;
    s_running = false;
    vTaskDeleteWithCaps(NULL);
}

namespace selftest {

bool start()
{
    if (s_running) return false;
    for (Step &st : s_steps) {
        st.st = St::Wait;
        st.detail[0] = 0;
    }
    s_running = true;
    s_done = false;
    ESP_LOGW(TAG, "self-test started");
    // stack in PSRAM: it only reads state and sleeps
    if (xTaskCreateWithCaps(task, "selftest", 4096, NULL, 4, NULL, MALLOC_CAP_SPIRAM) != pdPASS) {
        s_running = false;
        return false;
    }
    return true;
}

cJSON *json()
{
    static const char *const NAMES[] = {"wait", "run", "pass", "warn", "fail"};
    cJSON *r = cJSON_CreateObject();
    bool ok = true;
    cJSON *a = cJSON_AddArrayToObject(r, "steps");
    for (const Step &st : s_steps) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", st.name);
        cJSON_AddStringToObject(o, "state", NAMES[(int) st.st]);
        cJSON_AddStringToObject(o, "detail", st.detail);
        cJSON_AddItemToArray(a, o);
        ok &= st.st != St::Fail;
    }
    cJSON_AddBoolToObject(r, "running", s_running);
    cJSON_AddBoolToObject(r, "done", s_done);
    cJSON_AddBoolToObject(r, "passed", s_done && ok);
    return r;
}

}
