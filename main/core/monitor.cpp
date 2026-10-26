#include "monitor.h"

#include <stdio.h>
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "core/board.h"
#include "core/settings.h"
#include "core/state.h"
#include "hw/sensors.h"
#include "mining/miner.h"
#include "net/wifi.h"

static const char *TAG = "monitor";

#define VR_FAN_TARGET_C 65.0f
#define COOLDOWN_HYST_C 20.0f
#define RECOVERY_FREQ_DROP 50
#define MAX_FAULT_RETRIES 3
#define MAX_OVERHEAT_RETRIES 3
#define RECOVERY_WAIT_S 30  // between recovery attempts, so a failing power-up is not hammered
#define SENSOR_LOSS_S 3  // consecutive failed temperature reads before cutting power
// Over-temperature and over-power cut-offs need this many consecutive readings:
// a single corrupted sensor read must not shut the miner down.
#define CRIT_CONFIRM_S 2
// A fan driven at least this hard but turning slower than this, for this long, has
// stalled. Only fans seen spinning since boot count: a fan that is not fitted reads
// 0 rpm too, and must not keep a one-fan miner from running.
#define FAN_STALL_MIN_DUTY 30.0f
#define FAN_STALL_RPM 300
#define FAN_STALL_S 10
// Within this many degrees of the tuner's ceiling the fans go to 100%, whatever the
// fan target says: the tuner must never back off for heat while cooling is left.
#define FAN_BOOST_MARGIN_C 3.0f

// Small PI controller; output is fan duty in percent.
struct FanPI {
    float integral = 0;
    float update(float temp, float target, float min_pct)
    {
        if (temp <= 0) return 100;  // sensor lost: fail safe
        float err = temp - target;
        integral += err * 0.5f;
        if (integral < -40) integral = -40;
        if (integral > 100) integral = 100;
        float out = 50 + err * 6.0f + integral;
        if (out < min_pct) out = min_pct;
        if (out > 100) out = 100;
        return out;
    }
};

// The last emergency, with the readings that caused it. Kept in RTC memory, so it
// survives software restarts and crashes (not a power cut) and the next boot can
// still say what happened.
#define EMERGENCY_NOTE_MAGIC 0x454d4752
static RTC_NOINIT_ATTR struct {
    uint32_t magic;
    uint32_t count;
    char text[140];
} s_note;

static void emergency(SysStatus status, const char *why)
{
    ESP_LOGE(TAG, "EMERGENCY: %s", why);
    if (s_note.magic != EMERGENCY_NOTE_MAGIC) s_note.count = 0;
    s_note.count++;
    snprintf(s_note.text, sizeof(s_note.text), "%s | chip %.1fC, VR %.1fC, %.1f W, %.2f V in, %u MHz @ %u mV, up %lus",
             why, g_state.chip_temp_max, g_state.vr_temp, g_state.pin, g_state.vin, board::freq_mhz(), board::core_mv(),
             (unsigned long) g_state.uptime_s);
    s_note.magic = EMERGENCY_NOTE_MAGIC;
    ESP_LOGE(TAG, "readings: %s", s_note.text);
    miner::pause(true);
    board::shutdown();
    g_state.status = status;
    snprintf(g_state.fault, sizeof(g_state.fault), "%s", why);
}

static void monitor_task(void *)
{
    FanPI asic_pi, vr_pi;
    int tick = 0;
    int fault_retries = 0;
    int overheat_retries = 0, recovery_wait = 0;
    int sensor_bad = 0;
    int crit_chip = 0, crit_vr = 0, crit_power = 0;
    int stall[2] = {0, 0};
    char why[64];

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        g_state.uptime_s++;
        board::read_telemetry();
        Settings &cfg = settings::get();
        const board::Limits &lim = board::limits();
        bool sensors_ok = g_state.temp_ok && g_state.vr_temp_ok;

        // A header counts as connected once its fan has been seen spinning; an
        // empty header reads 0 rpm and must not trip the stall check.
        for (int i = 0; i < 2; i++) {
            if (g_state.fan_rpm[i] >= FAN_STALL_RPM && !g_state.fan_present[i]) {
                g_state.fan_present[i] = true;
                ESP_LOGI(TAG, "fan %d connected (%u rpm)", i + 1, g_state.fan_rpm[i]);
            }
        }
        if (g_state.uptime_s == 60) {
            for (int i = 0; i < 2; i++)
                if (!g_state.fan_present[i]) ESP_LOGW(TAG, "fan header %d: nothing connected (0 rpm since boot)", i + 1);
        }

        if (board::is_powered()) {
            // a lost sensor reads as -1 here, which the PI treats as "full fan"
            float a = asic_pi.update(g_state.temp_ok ? g_state.chip_temp_max : -1, cfg.fan_target_c, cfg.fan_min_pct);
            float v = vr_pi.update(g_state.vr_temp_ok ? g_state.vr_temp : -1, VR_FAN_TARGET_C, cfg.fan_min_pct);
#if CONFIG_AF_BOARD_SUPRAHEX
            // both Hex fans blow across chips and regulator alike
            float duty[2] = {v > a ? v : a, v > a ? v : a};
#else
            float duty[2] = {a, v > a ? v : a};  // header 1: ASIC fan, header 2: VR fan
#endif
            // manual headers hold their duty, except when the miner needs the air:
            // lost sensors and the last few degrees before a ceiling always mean 100 %
            for (int i = 0; i < 2; i++)
                if (cfg.fan_mode[i] == 1) duty[i] = sensors_ok ? cfg.fan_manual[i] : 100;
            if (g_state.chip_temp_max >= CONFIG_AF_MAX_CHIP_TEMP - FAN_BOOST_MARGIN_C) duty[0] = duty[1] = 100;
            if (g_state.vr_temp >= lim.vr_ceil_c - FAN_BOOST_MARGIN_C) duty[1] = 100;
            for (int i = 0; i < 2; i++) {
                fan::set_pct(i, duty[i]);
                g_state.fan_duty[i] = (uint8_t) duty[i];
            }
            g_state.fan_pct = (uint8_t) duty[0];

            // ---- hard limits: these cut power, no questions asked ----
            if (g_state.chip_stall) {
                snprintf(why, sizeof(why), "Chip %d stopped hashing", g_state.chip_stall - 1);
                g_state.chip_stall = 0;
                emergency(SysStatus::Fault, why);
                continue;
            }
            fan::check();  // re-applies the fan setup if the controller lost it
            // With one temperature sensor, a dead fan over the far chips would only
            // show up once they were already too hot.
            int stalled = -1;
            for (int i = 0; i < 2; i++) {
                bool slow = g_state.fan_present[i] && duty[i] >= FAN_STALL_MIN_DUTY && g_state.fan_rpm[i] < FAN_STALL_RPM;
                stall[i] = slow ? stall[i] + 1 : 0;
                if (stall[i] >= FAN_STALL_S) stalled = i;
            }
            if (stalled >= 0) {
                snprintf(why, sizeof(why), "Fan %d stalled (%u rpm at %.0f%%)", stalled + 1, g_state.fan_rpm[stalled],
                         duty[stalled]);
                stall[0] = stall[1] = 0;
                emergency(SysStatus::Fault, why);  // shutdown also runs both fans at 100%
                continue;
            }
            sensor_bad = sensors_ok ? 0 : sensor_bad + 1;
            if (sensor_bad >= SENSOR_LOSS_S) {
                snprintf(why, sizeof(why), "%s temperature sensor not responding", g_state.temp_ok ? "VR" : "Chip");
                emergency(SysStatus::Fault, why);
                continue;
            }
            crit_chip = g_state.chip_temp_max >= CONFIG_AF_CRIT_CHIP_TEMP ? crit_chip + 1 : 0;
            crit_vr = g_state.vr_temp >= lim.vr_crit_c ? crit_vr + 1 : 0;
            crit_power = g_state.pin > lim.max_power_w * 1.15f ? crit_power + 1 : 0;
            if (crit_chip >= CRIT_CONFIRM_S) {
                snprintf(why, sizeof(why), "Chip %.0fC >= %dC", g_state.chip_temp_max, CONFIG_AF_CRIT_CHIP_TEMP);
                crit_chip = crit_vr = crit_power = 0;
                emergency(SysStatus::Overheat, why);
                continue;
            }
            if (crit_vr >= CRIT_CONFIRM_S) {
                snprintf(why, sizeof(why), "VR %.0fC >= %.0fC", g_state.vr_temp, lim.vr_crit_c);
                crit_chip = crit_vr = crit_power = 0;
                emergency(SysStatus::Overheat, why);
                continue;
            }
            if (crit_power >= CRIT_CONFIRM_S) {
                snprintf(why, sizeof(why), "Power %.0fW over limit", g_state.pin);
                crit_chip = crit_vr = crit_power = 0;
                emergency(SysStatus::Fault, why);
                continue;
            }
            if (++tick % 5 == 0) {
                board::Error e = board::check_faults();
                if (e != board::Error::None) {
                    emergency(SysStatus::Fault, board::error_text(e));
                    continue;
                }
            }
            if (g_state.status != SysStatus::Overheat && g_state.status != SysStatus::Fault) {
                if (wifi::in_setup_mode()) g_state.status = SysStatus::SetupAP;
                else if (!wifi::connected()) g_state.status = SysStatus::Connecting;
                else g_state.status = g_state.pool_connected ? SysStatus::Mining : SysStatus::Connecting;
            }
        } else if (g_state.status == SysStatus::Overheat && overheat_retries < MAX_OVERHEAT_RETRIES) {
            fan::set_pct(0, 100);
            fan::set_pct(1, 100);
            // wait until well below the trip point, then restart lower
            if (sensors_ok && g_state.chip_temp_max < CONFIG_AF_CRIT_CHIP_TEMP - COOLDOWN_HYST_C &&
                g_state.vr_temp < lim.vr_crit_c - COOLDOWN_HYST_C && ++recovery_wait >= RECOVERY_WAIT_S) {
                recovery_wait = 0;
                overheat_retries++;
                uint16_t f = board::clamp_freq((cfg.tuned_freq ? cfg.tuned_freq : cfg.freq_mhz) - RECOVERY_FREQ_DROP);
                uint16_t mv = cfg.tuned_mv ? cfg.tuned_mv : cfg.core_mv;
                ESP_LOGW(TAG, "cooled down, restarting at %u MHz (%d/%d)", f, overheat_retries, MAX_OVERHEAT_RETRIES);
                if (board::power_up(f, mv) == board::Error::None) {
                    // only a point that actually came up becomes the new tuned point
                    if (cfg.autotune) {
                        cfg.tuned_freq = f;
                        cfg.tuned_mv = mv;
                        settings::save();
                    }
                    g_state.status = SysStatus::Mining;
                    g_state.fault[0] = 0;
                    miner::pause(false);
                }
            }
        } else if (g_state.status == SysStatus::Fault && fault_retries < MAX_FAULT_RETRIES) {
            static int wait = 0;
            if (++wait >= 30) {
                wait = 0;
                fault_retries++;
                ESP_LOGW(TAG, "retrying after fault (%d/%d)", fault_retries, MAX_FAULT_RETRIES);
                uint16_t f = cfg.tuned_freq ? cfg.tuned_freq : cfg.freq_mhz;
                uint16_t mv = cfg.tuned_mv ? cfg.tuned_mv : cfg.core_mv;
                if (board::power_up(board::clamp_freq(f - RECOVERY_FREQ_DROP), mv) == board::Error::None) {
                    g_state.status = SysStatus::Mining;
                    g_state.fault[0] = 0;
                    miner::pause(false);
                }
            }
        }
    }
}

namespace monitor {
const char *last_emergency() { return s_note.magic == EMERGENCY_NOTE_MAGIC ? s_note.text : ""; }
uint32_t emergency_count() { return s_note.magic == EMERGENCY_NOTE_MAGIC ? s_note.count : 0; }
void start() { xTaskCreate(monitor_task, "monitor", 4096, NULL, 15, NULL); }
}
