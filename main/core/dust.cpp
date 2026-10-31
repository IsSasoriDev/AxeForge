#include "dust.h"

#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "nvs.h"

#include "core/board.h"
#include "core/settings.h"
#include "core/state.h"
#include "net/notify.h"

static const char *TAG = "dust";

#define NS "af_dust"
#define WARMUP_MIN 30          // after a power-up, before samples count
#define POWER_BAND 0.10f       // a sample counts within this of the reference power
#define MIN_SAMPLES_DAY 240    // 4 hours of steady mining make a day count
#define BASELINE_DAYS 7
#define WINDOW_DAYS 7
#define STALE_DAYS 3           // this many mining days off the reference power: learn again
#define WARN_FAN_PCT 12.0f     // fan duty points above the baseline
#define WARN_TEMP_C 5.0f       // chip degrees above the baseline (fan at full speed, or both)
#define FAN_PER_C 2.0f         // fan duty points per degree of room temperature
#define ROOM_PER_OUTDOOR 0.3f  // how much of an outdoor change reaches the room

struct Day {
    float t, d, o;  // average chip temp, fan duty, outdoor temp (NAN if unknown)
};

static struct Store {
    uint32_t magic;
    float ref_w;              // reference power, 0 = not set yet
    uint8_t base_days;        // days in the baseline so far
    Day base;                 // running mean of the baseline days
    uint8_t n;                // days in the window ring
    uint8_t head;
    Day win[WINDOW_DAYS];
    uint8_t off_days;         // mining days with no sample at the reference power
    bool warned;
} s_st;
#define MAGIC 0x44555354

// today's running sums
static double s_t, s_d, s_o;
static int s_n, s_on, s_minutes, s_mining_minutes;
static int s_powered_min = 0;

static void save()
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "st", &s_st, sizeof(s_st));
    nvs_commit(h);
    nvs_close(h);
}

static void start_over(float ref_w)
{
    memset(&s_st, 0, sizeof(s_st));
    s_st.magic = MAGIC;
    s_st.ref_w = ref_w;
    s_st.base.o = NAN;
    save();
}

static Day window_mean()
{
    Day m = {0, 0, 0};
    int no = 0;
    for (int i = 0; i < s_st.n; i++) {
        m.t += s_st.win[i].t;
        m.d += s_st.win[i].d;
        if (!isnan(s_st.win[i].o)) {
            m.o += s_st.win[i].o;
            no++;
        }
    }
    if (s_st.n) {
        m.t /= s_st.n;
        m.d /= s_st.n;
    }
    m.o = no ? m.o / no : NAN;
    return m;
}

// Drift against the baseline, with what the outdoor temperature explains taken out.
static void drift(float *dt, float *dd)
{
    Day c = window_mean();
    float room = !isnan(c.o) && !isnan(s_st.base.o) ? (c.o - s_st.base.o) * ROOM_PER_OUTDOOR : 0;
    *dt = c.t - s_st.base.t - room;
    *dd = c.d - s_st.base.d - room * FAN_PER_C;
}

static void close_day()
{
    bool counted = s_n >= MIN_SAMPLES_DAY;
    if (!counted) {
        // mined most of the day but never at the reference power: it was retuned
        if (s_mining_minutes >= MIN_SAMPLES_DAY && ++s_st.off_days >= STALE_DAYS) {
            ESP_LOGI(TAG, "the miner runs at a different power now: learning again");
            start_over(0);
        }
        return;
    }
    s_st.off_days = 0;
    Day d = {(float) (s_t / s_n), (float) (s_d / s_n), s_on > s_n / 2 ? (float) (s_o / s_on) : NAN};
    if (s_st.base_days < BASELINE_DAYS) {
        int k = s_st.base_days;
        s_st.base.t = (s_st.base.t * k + d.t) / (k + 1);
        s_st.base.d = (s_st.base.d * k + d.d) / (k + 1);
        if (!isnan(d.o)) s_st.base.o = isnan(s_st.base.o) || k == 0 ? d.o : (s_st.base.o * k + d.o) / (k + 1);
        s_st.base_days++;
        ESP_LOGI(TAG, "baseline day %u of %d: chips %.1f C, fan %.0f%%", s_st.base_days, BASELINE_DAYS, d.t, d.d);
    } else {
        s_st.win[s_st.head] = d;
        s_st.head = (s_st.head + 1) % WINDOW_DAYS;
        if (s_st.n < WINDOW_DAYS) s_st.n++;
        if (s_st.n >= 3) {
            float dt, dd;
            drift(&dt, &dd);
            Day c = window_mean();
            bool dirty = dd >= WARN_FAN_PCT || dt >= WARN_TEMP_C;
            ESP_LOGI(TAG, "week vs baseline: chips %+.1f C, fan %+.0f points%s", dt, dd, dirty ? ": clean the heatsink" : "");
            if (dirty && !s_st.warned) {
                s_st.warned = true;
                notify::post(NOTIFY_MAINTENANCE,
                             "time to clean the heatsink. The chips have been running %.0f C warmer than usual for a week "
                             "at the same power, and the fan is working harder to keep up (%.0f%% now, %.0f%% when it was "
                             "clean). Room temperature doesn't explain it, so it's most likely dust on the heatsink or "
                             "fan. Switch the miner off, blow the dust out of the heatsink and fan, then press \"Done, I "
                             "cleaned it\" on the dashboard.",
                             dt > 0 ? dt : 0, c.d, s_st.base.d);
            }
            if (!dirty && dt < WARN_TEMP_C / 2 && dd < WARN_FAN_PCT / 2) s_st.warned = false;
        }
    }
    save();
}

namespace dust {

void load()
{
    nvs_handle_t h;
    size_t n = sizeof(s_st);
    bool ok = false;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        ok = nvs_get_blob(h, "st", &s_st, &n) == ESP_OK && n == sizeof(s_st) && s_st.magic == MAGIC;
        nvs_close(h);
    }
    if (!ok) {
        memset(&s_st, 0, sizeof(s_st));
        s_st.magic = MAGIC;
        s_st.base.o = NAN;
    }
}

void tick_minute()
{
    const MinerState &g = g_state;
    s_powered_min = board::is_powered() ? s_powered_min + 1 : 0;
    bool mining = board::is_powered() && g.status == SysStatus::Mining;
    if (mining) s_mining_minutes++;
    // steady: past warm-up, the fan on its automatic loop, the tuner not stepping
    bool steady = mining && s_powered_min >= WARMUP_MIN && g.fan_present[0] && settings::get().fan_mode[0] == 0 &&
                  (g.tuner.phase == TunerPhase::Locked || g.tuner.phase == TunerPhase::Off) && g.pin > 1 &&
                  g.temp_ok;
    if (steady) {
        if (s_st.ref_w <= 0 && s_powered_min >= WARMUP_MIN + 60) {
            s_st.ref_w = g.pin;  // after an hour of steady mining, this is "usual"
            ESP_LOGI(TAG, "learning how this miner runs at %.0f W", s_st.ref_w);
        }
        if (s_st.ref_w > 0 && fabsf(g.pin - s_st.ref_w) <= POWER_BAND * s_st.ref_w) {
            s_t += g.chip_temp_max;
            s_d += g.fan_duty[0];
            s_n++;
            if (g.weather.valid) {
                s_o += g.weather.temp;
                s_on++;
            }
        }
    }
    if (++s_minutes >= 24 * 60) {
        close_day();
        s_t = s_d = s_o = 0;
        s_n = s_on = s_minutes = s_mining_minutes = 0;
    }
}

void reset()
{
    ESP_LOGI(TAG, "heatsink cleaned: learning again");
    start_over(0);
    s_t = s_d = s_o = 0;
    s_n = s_on = s_minutes = s_mining_minutes = 0;
}

State state()
{
    if (s_st.base_days < BASELINE_DAYS || s_st.n < 3) return State::Learning;
    float dt, dd;
    drift(&dt, &dd);
    return dd >= WARN_FAN_PCT || dt >= WARN_TEMP_C ? State::Dirty : State::Ok;
}

cJSON *json()
{
    cJSON *r = cJSON_CreateObject();
    State st = state();
    cJSON_AddStringToObject(r, "state", st == State::Dirty ? "dirty" : st == State::Ok ? "ok" : "learning");
    cJSON_AddNumberToObject(r, "baseDays", s_st.base_days);
    cJSON_AddNumberToObject(r, "weekDays", s_st.n);
    cJSON_AddNumberToObject(r, "refW", roundf(s_st.ref_w));
    if (s_st.base_days) {
        cJSON_AddNumberToObject(r, "baseTemp", roundf(s_st.base.t * 10) / 10);
        cJSON_AddNumberToObject(r, "baseFan", roundf(s_st.base.d));
    }
    if (s_st.n) {
        Day c = window_mean();
        float dt, dd;
        drift(&dt, &dd);
        cJSON_AddNumberToObject(r, "temp", roundf(c.t * 10) / 10);
        cJSON_AddNumberToObject(r, "fan", roundf(c.d));
        cJSON_AddNumberToObject(r, "dTemp", roundf(dt * 10) / 10);
        cJSON_AddNumberToObject(r, "dFan", roundf(dd));
    }
    return r;
}

}
