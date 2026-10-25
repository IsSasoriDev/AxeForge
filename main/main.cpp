// AxeForge - firmware for the NerdQaxe++ (4x BM1370) with a 3.5" 480x320 display.

#include <stdio.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "core/board.h"
#include "core/logbuf.h"
#include "core/monitor.h"
#include "core/settings.h"
#include "core/state.h"
#include "core/tuner.h"
#include "mining/miner.h"
#include "net/mempool.h"
#include "net/weather.h"
#include "net/web.h"
#include "net/wifi.h"
#include "stratum/pools.h"
#include "ui/display.h"
#include "ui/layout_store.h"

static const char *TAG = "main";

MinerState g_state;

extern "C" void app_main(void)
{
    logbuf::init();  // first, so the web console shows the whole boot
    // the SV2 Noise code dumps handshake key material at INFO; never let that reach the web console
    esp_log_level_set("sv2_noise", ESP_LOG_WARN);

    static const char *reasons[] = {"unknown", "power-on", "external", "software", "panic", "int-wdt", "task-wdt",
                                    "wdt", "deep-sleep", "brownout", "sdio", "usb", "jtag", "efuse", "pwr-glitch", "cpu-lockup"};
    esp_reset_reason_t rr = esp_reset_reason();
    snprintf(g_state.reset_reason, sizeof(g_state.reset_reason), "%s", rr < 16 ? reasons[rr] : "other");
    ESP_LOGW(TAG, "reset reason: %s", g_state.reset_reason);
    // RTC memory is only kept over a software restart; anything else leaves junk
    if (rr == ESP_RST_SW && web::previous_restart()[0]) ESP_LOGW(TAG, "previous restart: %s", web::previous_restart());
    else if (rr != ESP_RST_SW) web::previous_restart();  // read once so stale RTC contents are cleared
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    settings::load();
    Settings &cfg = settings::get();
    layout_store::load();

    display::start();

    board::Error e = board::init_hardware();
    monitor::start();  // telemetry + fans + emergency cut-off, before any power-up
    wifi::start();
    web::start();
    weather::start();
    mempool::start();

    if (e != board::Error::None) {
        g_state.status = SysStatus::Fault;
        snprintf(g_state.fault, sizeof(g_state.fault), "%s", board::error_text(e));
        ESP_LOGE(TAG, "hardware init failed: %s", g_state.fault);
        return;  // web UI and display stay up so the user can see why
    }

    uint16_t freq = cfg.freq_mhz, mv = cfg.core_mv;
    if (cfg.autotune && cfg.tuned_freq) {
        freq = cfg.tuned_freq;
        mv = cfg.tuned_mv;
    }
    ESP_LOGI(TAG, "powering ASICs at %u MHz / %u mV", freq, mv);
    e = board::power_up(freq, mv);
    if (e != board::Error::None) {
        // the monitor retries a few times; mining tasks idle until power is up
        g_state.status = SysStatus::Fault;
        snprintf(g_state.fault, sizeof(g_state.fault), "%s", board::error_text(e));
        ESP_LOGE(TAG, "power up failed: %s", g_state.fault);
    }

    pools::start();
    miner::start();
    tuner::start();
    ESP_LOGI(TAG, "boot done: internal RAM free %u, largest DMA block %u, PSRAM free %u",
             (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
             (unsigned) heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
