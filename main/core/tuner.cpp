#include "tuner.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "asic/bm13xx.h"
#include "core/board.h"
#include "core/gates.h"
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
#define CHIP_STEP 12            // MHz per per-chip raise
#define CHIP_MAX_MHZ 48         // no chip more than this above the chain clock
#define EFF_WORSE_STEPS 2       // efficiency goal: stop climbing after this many worse steps
#define EFF_MARGIN 0.01f        // a J/TH difference smaller than this is window noise, not a trend

enum Goal : uint8_t { GOAL_HASHRATE = 0, GOAL_POWER_CAP = 1, GOAL_EFFICIENCY = 2 };

enum class Verdict { Stable, Errors, Thermal, Power, Aborted };

struct Result {
    Verdict verdict;
    float ghs, err, worst_chip, tmax, vrmax, pmax, pavg;
    float chip_w;  // average regulator output power: what the chips draw, without the fans
};

static volatile bool s_restart = false;
static volatile uint16_t s_hold = 0;  // clock cap after a fault retry, 0 = none

// The clock actually run for a requested one, after any fault hold. Everything the
// tuner records goes through this, so it never saves a point it didn't measure.
static uint16_t held(uint16_t f)
{
    uint16_t h = s_hold;
    return h && f > h ? h : f;
}

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

// The input power the tuner may use: the board's limit, or the user's cap below it.
static float power_ceiling()
{
    const Settings &cfg = settings::get();
    float lim = gates::max_power();
    if (cfg.tune_goal == GOAL_POWER_CAP && cfg.power_cap_w > 0 && cfg.power_cap_w < lim) return cfg.power_cap_w;
    return lim;
}

// From the chips' own power where the regulator reports it: input power also
// carries the fans, whose speed changes between steps more than one clock step does.
static float joules_per_th(const Result &r)
{
    float w = r.chip_w > 0 ? r.chip_w : r.pavg;
    return r.ghs > 0 ? w / (r.ghs / 1000.0f) : 1e9f;
}

// Applies per-chip offsets, keeps them in the settings (saved by the caller) and
// counts them in the expected hashrate the stability check compares against.
static void set_offsets(const int8_t *off)
{
    Settings &cfg = settings::get();
    if (off != cfg.chip_off) memcpy(cfg.chip_off, off, sizeof(cfg.chip_off));
    asic::set_chip_offsets(cfg.chip_off, board::is_powered());
    g_state.expected_ghs =
        (board::freq_mhz() * g_state.chips_detected + asic::offset_total()) * (float) asic::SMALL_CORES / 1000.0f;
}

static bool any_offset()
{
    for (int i = 0; i < AF_NUM_ASICS; i++)
        if (settings::get().chip_off[i]) return true;
    return false;
}

static void clear_offsets()
{
    int8_t zero[AF_NUM_ASICS] = {};
    set_offsets(zero);
}

// Applies a point in a safe order: voltage up before frequency up,
// frequency down before voltage down.
static bool apply(uint16_t freq, uint16_t mv)
{
    freq = board::clamp_freq(held(freq));
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
    Result r = {Verdict::Stable, 0, 0, 0, 0, 0, 0, 0, 0};
    const float t_ceil = CONFIG_AF_MAX_CHIP_TEMP;
    const float vr_ceil = board::limits().vr_ceil_c;
    const float p_ceil = power_ceiling();
    const int total = warmup_s + window_s;
    int hot = 0, watts = 0;
    double psum = 0, csum = 0;
    int pn = 0, cn = 0;

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
        if (t >= warmup_s) {
            psum += g_state.pin;
            pn++;
            float cw = g_state.vout * g_state.iout;
            if (!isnan(cw) && g_state.vout > 0.3f && g_state.iout > 0.1f) {
                csum += cw;
                cn++;
            }
        }

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
    r.pavg = pn ? (float) (psum / pn) : 0;
    r.chip_w = cn > pn / 2 ? (float) (csum / cn) : 0;  // only if the regulator reported most of the window
    r.worst_chip = miner::window_worst_chip_err();
    float eff = g_state.expected_ghs > 0 ? r.ghs / g_state.expected_ghs : 0;
    // the weakest chip fails first; an average over four chips can hide it
    if (r.err > max_err || r.worst_chip > max_chip_err || eff < MIN_EFFICIENCY) r.verdict = Verdict::Errors;
    return r;
}

static void lock_point(uint16_t freq, uint16_t mv, float ghs)
{
    freq = held(freq);
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
    // efficiency goal: hold the voltage (raising it costs more than it gains) and
    // keep the point with the fewest joules per terahash
    const bool eff_goal = settings::get().tune_goal == GOAL_EFFICIENCY;
    float best_jth = 1e9f;
    int worse = 0;

    while (true) {
        g_state.tuner.phase = TunerPhase::Measuring;
        note("Testing %.0f MHz @ %.0f mV", f, mv);
        Result r = measure(WARMUP_S, WINDOW_S, MAX_ERR_PCT, WORST_CHIP_PCT);
        g_state.tuner.steps_done++;
        if (r.verdict == Verdict::Aborted) return false;
        ESP_LOGI(TAG, "%u MHz %u mV: %s, %.0f GH/s, err %.2f%% (worst chip %.2f%%), tmax %.1f, vr %.1f, %.0f W", f, mv,
                 verdict_text(r.verdict), r.ghs, r.err, r.worst_chip, r.tmax, r.vrmax, r.pmax);

        if (r.verdict == Verdict::Stable) {
            float jth = joules_per_th(r);
            // a step only counts as better or worse beyond the noise of one window;
            // in between, the climb goes on without counting it either way
            bool better = eff_goal ? jth < best_jth * (1 - EFF_MARGIN) : r.ghs > best_ghs;
            bool clearly_worse = eff_goal && jth > best_jth * (1 + EFF_MARGIN);
            if (eff_goal) ESP_LOGI(TAG, "%u MHz: %.2f J/TH (%s power)", f, jth, r.chip_w > 0 ? "chip" : "input");
            if (better) {
                best_f = f;
                best_mv = mv;
                best_ghs = r.ghs;
                best_jth = jth;
                worse = 0;
                g_state.tuner.best_freq = f;
                g_state.tuner.best_mv = mv;
                g_state.tuner.best_ghs = r.ghs;
            } else if (clearly_worse && ++worse >= EFF_WORSE_STEPS) {
                note("Efficiency peaked at %.0f MHz (%.1f J/TH)", best_f, best_jth);
                return true;
            }
            bumps = 0;
            bool near_thermal = r.tmax > CONFIG_AF_MAX_CHIP_TEMP - THERMAL_MARGIN_C ||
                                r.vrmax > board::limits().vr_ceil_c - THERMAL_MARGIN_C;
            bool near_power = r.pmax > power_ceiling() * 0.95f;
            bool at_hold = held(f + FREQ_STEP) != f + FREQ_STEP;
            if (near_thermal || near_power || at_hold || f + FREQ_STEP > gates::max_freq()) {
                note(near_power ? "Power ceiling reached" : near_thermal ? "Thermal ceiling reached"
                                : at_hold                ? "Held after a fault"
                                                         : "Max freq reached");
                return true;
            }
            f += FREQ_STEP;
            g_state.tuner.phase = TunerPhase::StepUp;
            apply(f, mv);
            continue;
        }

        if (r.verdict == Verdict::Errors && !eff_goal && bumps < MAX_MV_BUMPS && mv + MV_STEP <= gates::max_mv()) {
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

// What one chip should hash at the chain clock plus its offset.
static float chip_expected_ghs(uint16_t f, int off) { return (held(f) + off) * (float) asic::SMALL_CORES / 1000.0f; }

// Takes one raised chip down a step (watch phase), keeping the chain where it is.
static void lower_chip(int chip, const char *why)
{
    Settings &cfg = settings::get();
    int8_t off[AF_NUM_ASICS];
    memcpy(off, cfg.chip_off, sizeof(off));
    off[chip] = off[chip] > CHIP_STEP ? off[chip] - CHIP_STEP : 0;
    set_offsets(off);
    settings::save();
    snprintf(g_state.tuner.note, sizeof(g_state.tuner.note), "%s: chip %d back to +%d MHz", why, chip + 1, off[chip]);
    ESP_LOGW(TAG, "%s", g_state.tuner.note);
}

// A raised chip hashing below MIN_EFFICIENCY of its own clock over the last window, or -1.
static int slow_raised_chip(uint16_t f)
{
    const Settings &cfg = settings::get();
    for (int i = 0; i < g_state.chips_detected && i < AF_NUM_ASICS; i++)
        if (cfg.chip_off[i] > 0 && miner::window_chip_ghs(i) < MIN_EFFICIENCY * chip_expected_ghs(f, cfg.chip_off[i]))
            return i;
    return -1;
}

// Phase 2b: per-chip clocks. All chips that can go higher step up together; the
// per-chip error counts say which ones held. A chip that fails goes back one step
// and stays there. Returns false if aborted.
static bool tune_chips(uint16_t f, float &ghs)
{
    Settings &cfg = settings::get();
    if (!asic::PER_CHIP_CLOCK || board::is_rev7() || !cfg.chip_tune || cfg.tune_goal == GOAL_EFFICIENCY ||
        g_state.chips_detected < 2)
        return true;
    const int chips = g_state.chips_detected < AF_NUM_ASICS ? g_state.chips_detected : AF_NUM_ASICS;
    const int fmax = gates::max_freq();
    int8_t off[AF_NUM_ASICS];
    memcpy(off, cfg.chip_off, sizeof(off));
    bool frozen[AF_NUM_ASICS] = {};

    for (int round = 0; round < CHIP_MAX_MHZ / CHIP_STEP; round++) {
        bool raised[AF_NUM_ASICS] = {};
        int n = 0;
        for (int i = 0; i < chips; i++) {
            if (frozen[i] || off[i] + CHIP_STEP > CHIP_MAX_MHZ || held(f) + off[i] + CHIP_STEP > fmax) continue;
            off[i] += CHIP_STEP;
            raised[i] = true;
            n++;
        }
        if (!n) break;
        g_state.tuner.phase = TunerPhase::Measuring;
        snprintf(g_state.tuner.note, sizeof(g_state.tuner.note), "Per-chip: %d chip%s up %d MHz", n, n > 1 ? "s" : "",
                 CHIP_STEP);
        ESP_LOGI(TAG, "%s", g_state.tuner.note);
        set_offsets(off);
        Result r = measure(WARMUP_S / 2, WINDOW_S, MAX_ERR_PCT, 100.0f);  // chips are judged one by one below
        g_state.tuner.steps_done++;
        bool stop = r.verdict == Verdict::Thermal || r.verdict == Verdict::Power || r.verdict == Verdict::Aborted;
        int failed = 0;
        for (int i = 0; i < chips; i++) {
            if (!raised[i]) continue;
            float e = miner::window_chip_err(i);
            float ghs = miner::window_chip_ghs(i);
            float want = chip_expected_ghs(f, off[i]);
            // a chip over its own limit fails; with the total over its limit, so does
            // any raised chip above the total limit. A chip that drops work without
            // reporting errors fails on its hashrate.
            bool bad = stop || e > WORST_CHIP_PCT || ghs < MIN_EFFICIENCY * want ||
                       (r.verdict == Verdict::Errors && e > MAX_ERR_PCT);
            if (bad) {
                off[i] -= CHIP_STEP;
                frozen[i] = true;
                failed++;
            }
            ESP_LOGI(TAG, "chip %d at +%d MHz: %.0f of %.0f GH/s, %.2f%% errors%s", i + 1, off[i] + (bad ? CHIP_STEP : 0),
                     ghs, want, e, bad ? ", back one step" : "");
        }
        if (r.verdict == Verdict::Errors && !failed) {
            // the total failed without any one chip standing out: undo the round
            for (int i = 0; i < chips; i++)
                if (raised[i]) {
                    off[i] -= CHIP_STEP;
                    frozen[i] = true;
                }
            stop = true;
        }
        if (r.verdict == Verdict::Stable && !failed && r.ghs > ghs) ghs = r.ghs;
        if (stop || failed) set_offsets(off);
        if (r.verdict == Verdict::Aborted) {
            settings::save();
            return false;
        }
        if (stop) break;
    }
    set_offsets(off);
    settings::save();
    char line[8 * AF_NUM_ASICS + 1] = "";
    for (int i = 0; i < chips; i++)
        snprintf(line + strlen(line), sizeof(line) - strlen(line), "%s%+d", i ? " " : "", off[i]);
    snprintf(g_state.tuner.note, sizeof(g_state.tuner.note), "Per-chip offsets: %s MHz", line);
    ESP_LOGI(TAG, "%s", g_state.tuner.note);
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
        static uint16_t seen_freq = 0;
        if (cfg.freq_mhz != seen_freq) {  // a new manual clock lifts the cap
            if (seen_freq) s_hold = 0;
            seen_freq = cfg.freq_mhz;
        }
        if (!cfg.autotune) {
            uint16_t want = s_hold && cfg.freq_mhz > s_hold ? s_hold : cfg.freq_mhz;
            g_state.tuner.phase = TunerPhase::Off;
            snprintf(g_state.tuner.note, sizeof(g_state.tuner.note), "Manual: %u MHz @ %u mV", cfg.freq_mhz, cfg.core_mv);
            if (asic::offset_total() != 0 && board::is_powered()) {
                // a manual clock means every chip at it; the tuned offsets stay saved
                // and come back with auto-tune
                int8_t zero[AF_NUM_ASICS] = {};
                asic::set_chip_offsets(zero, true);
                g_state.expected_ghs = board::freq_mhz() * g_state.chips_detected * (float) asic::SMALL_CORES / 1000.0f;
            }
            if (board::is_powered() && (board::freq_mhz() != board::clamp_freq(want) ||
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
            clear_offsets();
            settings::save();
            g_state.tuner = TunerState();
            apply(cfg.freq_mhz, cfg.core_mv);
        }

        uint16_t best_f = board::freq_mhz(), best_mv = board::core_mv();
        float best_ghs = 0;

        if (!cfg.tuned_freq) {
            if (any_offset()) {  // a new search starts with every chip at the chain clock
                clear_offsets();
                settings::save();
            }
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
            if (!tune_chips(best_f, best_ghs)) continue;
        } else {
            set_offsets(cfg.chip_off);  // back from manual mode: the saved per-chip clocks again
            apply(cfg.tuned_freq, cfg.tuned_mv);
            best_f = held(cfg.tuned_freq);
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
                int slow = slow_raised_chip(best_f);
                if (slow >= 0) lower_chip(slow, "Slow");
                continue;
            }
            int wc = -1;
            miner::window_worst_chip_err(&wc);
            if (r.verdict == Verdict::Errors && wc >= 0 && wc < AF_NUM_ASICS && cfg.chip_off[wc] > 0) {
                lower_chip(wc, "Drift");  // a raised chip is the likely cause: not the chain
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
void restart()
{
    s_hold = 0;
    s_restart = true;
}

void hold(uint16_t mhz)
{
    s_hold = mhz;
    ESP_LOGW(TAG, "holding at or below %u MHz after a fault", mhz);
}
}
