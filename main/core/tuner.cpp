#include "tuner.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "core/board.h"
#include "core/settings.h"
#include "core/state.h"
#include "mining/miner.h"

static const char *TAG = "tuner";

#define FREQ_STEP 12            // MHz per climb step (ramped in 6.25 MHz PLL steps)
#define MV_STEP 10              // mV per voltage bump
#define MAX_MV_BUMPS 4          // voltage bumps allowed at one frequency before giving up
#define WARMUP_S 120            // let temps settle after a change
#define WINDOW_S 300            // measurement window
#define WATCH_WINDOW_S 600      // rolling check once locked
#define MAX_ERR_PCT 1.0f        // hw error rate that still counts as stable
#define WATCH_MAX_ERR_PCT 2.0f
#define WORST_CHIP_PCT 2.0f     // any one chip, while climbing
#define WATCH_WORST_CHIP_PCT 3.0f  // any one chip, once locked (chips without a sensor drift first)
#define MIN_EFFICIENCY 0.92f    // measured / theoretical hashrate
#define THERMAL_MARGIN_C 2.0f   // stop climbing this close to the ceiling
#define TRIM_MARGIN_MV 10       // added back after voltage trim

enum class Verdict { Stable, Errors, Thermal, Power, Aborted };

struct Result {
    Verdict verdict;
    float ghs, err, worst_chip, tmax, vrmax, pmax;
};

static volatile bool s_restart = false;

static const char *verdict_text(Verdict v)
{
    switch (v) {
    case Verdict::Stable: return "stable";
    case Verdict::Errors: return "hw errors";
    case Verdict::Thermal: return "thermal limit";
    case Verdict::Power: return "power limit";
    case Verdict::Aborted: return "aborted";
    }
    return "?";
}

static void note(const char *fmt, float a = 0, float b = 0)
{
    snprintf(g_state.tuner.note, sizeof(g_state.tuner.note), fmt, a, b);
    ESP_LOGI(TAG, "%s", g_state.tuner.note);
}

static bool should_abort() { return s_restart || !settings::get().autotune || !board::is_powered(); }

// Applies a point in a safe order: voltage up before frequency up,
// frequency down before voltage down.
static bool apply(uint16_t freq, uint16_t mv)
{
    freq = board::clamp_freq(freq);
    mv = board::clamp_mv(mv);
    g_state.tuner.trial_freq = freq;
    g_state.tuner.trial_mv = mv;
    bool ok = true;
    if (mv > board::core_mv()) ok &= board::set_core_mv(mv);
    if (freq != board::freq_mhz()) ok &= board::set_freq_mhz(freq);
    if (mv < board::core_mv()) ok &= board::set_core_mv(mv);
    return ok;
}

static Result measure(int warmup_s, int window_s, float max_err, float max_chip_err)
{
    Result r = {Verdict::Stable, 0, 0, 0, 0, 0, 0};
    const float t_ceil = CONFIG_AF_MAX_CHIP_TEMP;
    const float vr_ceil = board::limits().vr_ceil_c;
    const float p_ceil = board::limits().max_power_w;
    const int total = warmup_s + window_s;
    int hot = 0, watts = 0;

    for (int t = 0; t < total; t++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (should_abort()) {
            r.verdict = Verdict::Aborted;
            return r;
        }
        if (t == warmup_s) miner::reset_window();
        g_state.tuner.progress = (uint8_t) (100 * (t + 1) / total);

        if (g_state.chip_temp_max > r.tmax) r.tmax = g_state.chip_temp_max;
        if (g_state.vr_temp > r.vrmax) r.vrmax = g_state.vr_temp;
        if (g_state.pin > r.pmax) r.pmax = g_state.pin;

        // fail fast on clear limit violations (sustained, not single spikes)
        // no reading counts as too hot: never climb blind
        bool blind = !g_state.temp_ok || !g_state.vr_temp_ok;
        hot = (blind || g_state.chip_temp_max > t_ceil || g_state.vr_temp > vr_ceil) ? hot + 1 : 0;
        watts = (g_state.pin > p_ceil) ? watts + 1 : 0;
        if (hot >= 10) {
            r.verdict = Verdict::Thermal;
            return r;
        }
        if (watts >= 10) {
            r.verdict = Verdict::Power;
            return r;
        }
        if (t >= warmup_s + 60) {
            float ghs, err;
            miner::window_stats(&ghs, &err);
            if (err > max_err * 4) {
                r.ghs = ghs;
                r.err = err;
                r.verdict = Verdict::Errors;
                return r;
            }
        }
    }
    miner::window_stats(&r.ghs, &r.err);
    r.worst_chip = miner::window_worst_chip_err();
    float eff = g_state.expected_ghs > 0 ? r.ghs / g_state.expected_ghs : 0;
    // the weakest chip fails first; an average over four chips can hide it
    if (r.err > max_err || r.worst_chip > max_chip_err || eff < MIN_EFFICIENCY) r.verdict = Verdict::Errors;
    return r;
}

static void lock_point(uint16_t freq, uint16_t mv, float ghs)
{
    Settings &cfg = settings::get();
    cfg.tuned_freq = freq;
    cfg.tuned_mv = mv;
    settings::save();
    g_state.tuner.best_freq = freq;
    g_state.tuner.best_mv = mv;
    g_state.tuner.best_ghs = ghs;
    ESP_LOGI(TAG, "locked %u MHz @ %u mV (%.0f GH/s)", freq, mv, ghs);
}

// Phase 1: climb. Returns false if aborted.
static bool climb(uint16_t &best_f, uint16_t &best_mv, float &best_ghs)
{
    uint16_t f = board::freq_mhz();
    uint16_t mv = board::core_mv();
    int bumps = 0;

    while (true) {
        g_state.tuner.phase = TunerPhase::Measuring;
        note("Testing %.0f MHz @ %.0f mV", f, mv);
        Result r = measure(WARMUP_S, WINDOW_S, MAX_ERR_PCT, WORST_CHIP_PCT);
        g_state.tuner.steps_done++;
        if (r.verdict == Verdict::Aborted) return false;
        ESP_LOGI(TAG, "%u MHz %u mV: %s, %.0f GH/s, err %.2f%% (worst chip %.2f%%), tmax %.1f, vr %.1f, %.0f W", f, mv,
                 verdict_text(r.verdict), r.ghs, r.err, r.worst_chip, r.tmax, r.vrmax, r.pmax);

        if (r.verdict == Verdict::Stable) {
            if (r.ghs > best_ghs) {
                best_f = f;
                best_mv = mv;
                best_ghs = r.ghs;
                g_state.tuner.best_freq = f;
                g_state.tuner.best_mv = mv;
                g_state.tuner.best_ghs = r.ghs;
            }
            bumps = 0;
            bool near_thermal = r.tmax > CONFIG_AF_MAX_CHIP_TEMP - THERMAL_MARGIN_C ||
                                r.vrmax > board::limits().vr_ceil_c - THERMAL_MARGIN_C;
            bool near_power = r.pmax > board::limits().max_power_w * 0.95f;
            if (near_thermal || near_power || f + FREQ_STEP > board::limits().max_freq_mhz) {
                note(near_power ? "Power ceiling reached" : near_thermal ? "Thermal ceiling reached" : "Max freq reached");
                return true;
            }
            f += FREQ_STEP;
            g_state.tuner.phase = TunerPhase::StepUp;
            apply(f, mv);
            continue;
        }

        if (r.verdict == Verdict::Errors && bumps < MAX_MV_BUMPS && mv + MV_STEP <= CONFIG_AF_MAX_CORE_MV) {
            // more voltage usually fixes errors at a given clock
            mv += MV_STEP;
            bumps++;
            g_state.tuner.phase = TunerPhase::StepUp;
            note("Errors, raising voltage to %.0f mV", mv);
            apply(f, mv);
            continue;
        }

        snprintf(g_state.tuner.note, sizeof(g_state.tuner.note), "Wall: %s at %u MHz", verdict_text(r.verdict), f);
        return true;
    }
}

// Phase 2: lower the voltage at the best frequency while it stays stable.
static bool trim(uint16_t f, uint16_t &mv, float &ghs)
{
    bool trimmed = false;
    while (mv - MV_STEP >= CONFIG_AF_MIN_CORE_MV) {
        uint16_t trial = mv - MV_STEP;
        g_state.tuner.phase = TunerPhase::Measuring;
        note("Trimming: %.0f MHz @ %.0f mV", f, trial);
        apply(f, trial);
        Result r = measure(WARMUP_S / 2, WINDOW_S, MAX_ERR_PCT, WORST_CHIP_PCT);
        g_state.tuner.steps_done++;
        if (r.verdict == Verdict::Aborted) return false;
        if (r.verdict != Verdict::Stable || r.ghs < ghs * 0.98f) break;
        mv = trial;
        ghs = r.ghs;
        trimmed = true;
    }
    // The margin goes back only if the voltage came down; otherwise it would put the
    // lock above the point that was measured (and could push it over the power cap).
    if (trimmed) mv = board::clamp_mv(mv + TRIM_MARGIN_MV);
    apply(f, mv);
    return true;
}

static void tuner_task(void *)
{
    if (settings::get().autotune) {
        g_state.tuner.phase = TunerPhase::WarmUp;
        snprintf(g_state.tuner.note, sizeof(g_state.tuner.note), "Waiting for hashrate to settle");
    }
    vTaskDelay(pdMS_TO_TICKS(30000));  // let the pool connection and hashrate settle

    while (true) {
        Settings &cfg = settings::get();
        if (!cfg.autotune) {
            g_state.tuner.phase = TunerPhase::Off;
            snprintf(g_state.tuner.note, sizeof(g_state.tuner.note), "Manual: %u MHz @ %u mV", cfg.freq_mhz, cfg.core_mv);
            if (board::is_powered() && (board::freq_mhz() != board::clamp_freq(cfg.freq_mhz) ||
                                        board::core_mv() != board::clamp_mv(cfg.core_mv))) {
                apply(cfg.freq_mhz, cfg.core_mv);
            }
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        if (!board::is_powered() || !g_state.pool_connected) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        if (s_restart) {
            s_restart = false;
            cfg.tuned_freq = cfg.tuned_mv = 0;
            settings::save();
            g_state.tuner = TunerState();
            apply(cfg.freq_mhz, cfg.core_mv);
        }

        uint16_t best_f = board::freq_mhz(), best_mv = board::core_mv();
        float best_ghs = 0;

        if (!cfg.tuned_freq) {
            g_state.tuner.phase = TunerPhase::WarmUp;
            note("Starting search from %.0f MHz", best_f);
            if (!climb(best_f, best_mv, best_ghs)) continue;
            if (best_ghs <= 0) {
                // Nothing was stable, not even the start point: start lower and climb
                // again, rather than locking a point that was never measured.
                uint16_t lower = board::clamp_freq(best_f - 2 * FREQ_STEP);
                if (lower < best_f) {
                    note("Start point unstable, retrying from %.0f MHz", lower);
                    apply(lower, best_mv);
                    continue;
                }
                note("Unstable even at %.0f MHz", best_f);
                lock_point(best_f, best_mv, 0);
                continue;
            }
            apply(best_f, best_mv);
            if (!trim(best_f, best_mv, best_ghs)) continue;
            lock_point(best_f, best_mv, best_ghs);
        } else {
            apply(cfg.tuned_freq, cfg.tuned_mv);
            best_f = cfg.tuned_freq;
            best_mv = cfg.tuned_mv;
        }

        // Phase 3: watch. Back off one step if the point stops being stable.
        g_state.tuner.phase = TunerPhase::Locked;
        g_state.tuner.best_freq = best_f;
        g_state.tuner.best_mv = best_mv;
        while (!should_abort()) {
            snprintf(g_state.tuner.note, sizeof(g_state.tuner.note), "Locked %u MHz @ %u mV", best_f, best_mv);
            Result r = measure(0, WATCH_WINDOW_S, WATCH_MAX_ERR_PCT, WATCH_WORST_CHIP_PCT);
            if (r.verdict == Verdict::Aborted) break;
            if (r.verdict == Verdict::Stable) {
                if (r.ghs > 0) g_state.tuner.best_ghs = r.ghs;
                continue;
            }
            g_state.tuner.phase = TunerPhase::Backoff;
            best_f = board::clamp_freq(best_f - FREQ_STEP);
            snprintf(g_state.tuner.note, sizeof(g_state.tuner.note), "Drift (%s): backing off to %u MHz",
                     verdict_text(r.verdict), best_f);
            ESP_LOGW(TAG, "%s", g_state.tuner.note);
            apply(best_f, best_mv);
            lock_point(best_f, best_mv, r.ghs);
            g_state.tuner.phase = TunerPhase::Locked;
        }
    }
}

namespace tuner {
void start()
{
    if (xTaskCreate(tuner_task, "tuner", 4096, NULL, 4, NULL) != pdPASS) ESP_LOGE(TAG, "could not start tuner task");
}
void restart() { s_restart = true; }
}
