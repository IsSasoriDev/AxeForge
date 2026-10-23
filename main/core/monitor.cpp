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
            fan::set_pct(0, a);
            fan::set_pct(1, v > a ? v : a);
            g_state.fan_pct = (uint8_t) a;

            // ---- hard limits: these cut power, no questions asked ----
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
