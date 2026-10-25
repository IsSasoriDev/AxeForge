#include "monitor.h"

#include <stdio.h>
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
#define SENSOR_LOSS_S 3  // consecutive failed temperature reads before cutting power
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

static void emergency(SysStatus status, const char *why)
{
    ESP_LOGE(TAG, "EMERGENCY: %s", why);
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
    int sensor_bad = 0;
    int stall[2] = {0, 0};
    bool fan_seen[2] = {false, false};
    char why[64];

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        g_state.uptime_s++;
        board::read_telemetry();
        Settings &cfg = settings::get();
        const board::Limits &lim = board::limits();
        bool sensors_ok = g_state.temp_ok && g_state.vr_temp_ok;

        if (board::is_powered()) {
            // a lost sensor reads as -1 here, which the PI treats as "full fan"
            float a = asic_pi.update(g_state.temp_ok ? g_state.chip_temp_max : -1, cfg.fan_target_c, cfg.fan_min_pct);
            float v = vr_pi.update(g_state.vr_temp_ok ? g_state.vr_temp : -1, VR_FAN_TARGET_C, cfg.fan_min_pct);
            if (g_state.chip_temp_max >= CONFIG_AF_MAX_CHIP_TEMP - FAN_BOOST_MARGIN_C) a = 100;
            if (g_state.vr_temp >= lim.vr_ceil_c - FAN_BOOST_MARGIN_C) v = 100;
            fan::set_pct(0, a);
            fan::set_pct(1, v > a ? v : a);
            g_state.fan_pct = (uint8_t) a;

            // ---- hard limits: these cut power, no questions asked ----
            // With one temperature sensor, a dead fan over the far chips would only
            // show up once they were already too hot.
            const float duty[2] = {a, v > a ? v : a};
            int stalled = -1;
            for (int i = 0; i < 2; i++) {
                if (g_state.fan_rpm[i] >= FAN_STALL_RPM && !fan_seen[i]) {
                    fan_seen[i] = true;
                    ESP_LOGI(TAG, "fan %d spinning (%u rpm)", i + 1, g_state.fan_rpm[i]);
                }
                bool slow = fan_seen[i] && duty[i] >= FAN_STALL_MIN_DUTY && g_state.fan_rpm[i] < FAN_STALL_RPM;
                stall[i] = slow ? stall[i] + 1 : 0;
                if (stall[i] >= FAN_STALL_S) stalled = i;
            }
            if (g_state.uptime_s == 60) {
                for (int i = 0; i < 2; i++)
                    if (!fan_seen[i]) ESP_LOGW(TAG, "fan %d not detected (0 rpm since boot); stall check off for it", i + 1);
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
            if (g_state.chip_temp_max >= CONFIG_AF_CRIT_CHIP_TEMP) {
                snprintf(why, sizeof(why), "Chip %.0fC >= %dC", g_state.chip_temp_max, CONFIG_AF_CRIT_CHIP_TEMP);
                emergency(SysStatus::Overheat, why);
                continue;
            }
            if (g_state.vr_temp >= lim.vr_crit_c) {
                snprintf(why, sizeof(why), "VR %.0fC >= %.0fC", g_state.vr_temp, lim.vr_crit_c);
                emergency(SysStatus::Overheat, why);
                continue;
            }
            if (g_state.pin > lim.max_power_w * 1.15f) {
                snprintf(why, sizeof(why), "Power %.0fW over limit", g_state.pin);
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
        } else if (g_state.status == SysStatus::Overheat) {
            fan::set_pct(0, 100);
            fan::set_pct(1, 100);
            // wait until well below the trip point, then restart lower
            if (sensors_ok && g_state.chip_temp_max < CONFIG_AF_CRIT_CHIP_TEMP - COOLDOWN_HYST_C &&
                g_state.vr_temp < lim.vr_crit_c - COOLDOWN_HYST_C) {
                uint16_t f = board::clamp_freq((cfg.tuned_freq ? cfg.tuned_freq : cfg.freq_mhz) - RECOVERY_FREQ_DROP);
                uint16_t mv = cfg.tuned_mv ? cfg.tuned_mv : cfg.core_mv;
                ESP_LOGW(TAG, "cooled down, restarting at %u MHz", f);
                if (cfg.autotune) {
                    cfg.tuned_freq = f;
                    cfg.tuned_mv = mv;
                    settings::save();
                }
                if (board::power_up(f, mv) == board::Error::None) {
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
void start() { xTaskCreate(monitor_task, "monitor", 4096, NULL, 15, NULL); }
}
