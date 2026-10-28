#include "psu.h"

#include <math.h>
#include "esp_log.h"

#include "core/board.h"
#include "core/settings.h"  // NOTIFY_*
#include "core/state.h"
#include "net/notify.h"

static const char *TAG = "psu";

#define AVG_S 60          // averaging window under load
#define SETTLE_S 30       // after power-up, before readings count
#define WEAK_PCT 5.0f     // sag that earns a warning
#define BAD_PCT 8.0f      // sag where faults are close (12 V: 11.04 V, 5 V: 4.6 V)
#define CONFIRM_S 120     // a level has to hold this long before it is reported

static psu::State s_st;

namespace psu {

const State &state() { return s_st; }

const char *level_name(Level l)
{
    switch (l) {
    case Level::Ok: return "ok";
    case Level::Weak: return "weak";
    case Level::Bad: return "bad";
    default: return "unknown";
    }
}

void tick()
{
    static double sum_v = 0, sum_w = 0;
    static int n = 0, powered_s = 0, held_s = 0;
    static Level candidate = Level::Unknown;
    static bool told = false;

    float v = g_state.vin;
    if (isnan(v) || v < 1.0f) return;  // no reading on this board (yet)
    if (s_st.nominal == 0) s_st.nominal = v > 8.0f ? 12.0f : 5.0f;

    if (!board::is_powered()) {
        powered_s = 0;
        s_st.idle_v = v;
        return;
    }
    if (++powered_s < SETTLE_S) return;
    if (s_st.min_v == 0 || v < s_st.min_v) s_st.min_v = v;
    sum_v += v;
    sum_w += g_state.pin;
    if (++n < AVG_S) return;

    s_st.load_v = (float) (sum_v / n);
    s_st.load_w = (float) (sum_w / n);
    sum_v = sum_w = 0;
    n = 0;
    s_st.sag_pct = 100.0f * (s_st.nominal - s_st.load_v) / s_st.nominal;
    if (s_st.sag_pct < 0) s_st.sag_pct = 0;
    float amps = s_st.load_v > 0 ? s_st.load_w / s_st.load_v : 0;
    s_st.ohms = s_st.idle_v > s_st.load_v && amps > 0.5f ? (s_st.idle_v - s_st.load_v) / amps : 0;

    Level now = s_st.sag_pct >= BAD_PCT ? Level::Bad : s_st.sag_pct >= WEAK_PCT ? Level::Weak : Level::Ok;
    if (now != candidate) {
        candidate = now;
        held_s = 0;
    }
    held_s += AVG_S;
    if (held_s < CONFIRM_S && s_st.level != Level::Unknown) return;
    if (now == s_st.level) return;
    s_st.level = now;
    if (now == Level::Ok) {
        ESP_LOGI(TAG, "%.2f V under %.0f W (%.1f%% below %.0f V)", s_st.load_v, s_st.load_w, s_st.sag_pct, s_st.nominal);
        return;
    }
    ESP_LOGW(TAG, "supply %s: %.2f V under %.0f W, %.1f%% below %.0f V (lowest %.2f V, ~%.0f mOhm)", level_name(now),
             s_st.load_v, s_st.load_w, s_st.sag_pct, s_st.nominal, s_st.min_v, s_st.ohms * 1000);
    if (!told) {  // once per boot is enough to make the point
        told = true;
        notify::post(NOTIFY_FAULT, "power supply %s: %.2f V under %.0f W load (%.0f%% below %.0f V)", level_name(now),
                     s_st.load_v, s_st.load_w, s_st.sag_pct, s_st.nominal);
    }
}

}
