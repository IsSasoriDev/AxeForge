// AxeForge - firmware for the NerdQaxe++ (4x BM1370, 3.5" LCD) and the Bitaxe
// Supra Hex (6x BM1368, 128x32 OLED). The board is chosen at build time.

#include <stdio.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "core/board.h"
#include "core/logbuf.h"
#include "core/monitor.h"
#include "core/settings.h"
#include "core/state.h"
#include "core/tuner.h"
#include "mining/miner.h"
#include "net/captive.h"
#include "net/mempool.h"
#include "net/swarm.h"
#include "net/weather.h"
#include "net/web.h"
#include "net/wifi.h"
#include "stratum/pools.h"
#include "ui/display.h"
#include "ui/layout_store.h"

static const char *TAG = "main";

MinerState g_state;

#if CONFIG_AF_BOARD_SUPRAHEX
// Stock Bitaxe bootloaders have no app rollback, and AxeOS stays in the
// factory slot. If AxeForge crashes on boot three times running, hand the
// board back to AxeOS instead of looping until someone finds a USB cable.
#define BOOT_FAIL_LIMIT 3
#define BOOT_HEALTHY_S 120

static void fall_back_to_stock(const char *why)
{
    const esp_partition_t *factory =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (!factory || factory == esp_ota_get_running_partition()) {
        ESP_LOGE(TAG, "%s, and no stock firmware to fall back to", why);
        return;
    }
    ESP_LOGE(TAG, "%s - booting the stock firmware in '%s'", why, factory->label);
    if (esp_ota_set_boot_partition(factory) == ESP_OK) esp_restart();
}

static void set_boot_fails(uint8_t n)
{
    nvs_handle_t h;
    if (nvs_open("af_boot", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "bootfail", n);
    nvs_commit(h);
    nvs_close(h);
}

static void boot_guard(esp_reset_reason_t rr)
{
    bool crash = rr == ESP_RST_PANIC || rr == ESP_RST_INT_WDT || rr == ESP_RST_TASK_WDT || rr == ESP_RST_WDT ||
                 rr == ESP_RST_BROWNOUT;
    uint8_t fails = 0;
    nvs_handle_t h;
    // own namespace: settings::load() treats an empty "axeforge" namespace as a first boot
    if (nvs_open("af_boot", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, "bootfail", &fails);
        nvs_close(h);
    }
    fails = crash ? fails + 1 : 0;
    if (fails >= BOOT_FAIL_LIMIT) {
        set_boot_fails(0);
        fall_back_to_stock("crashed during startup 3 times in a row");
    }
    set_boot_fails(fails);
    if (fails) ESP_LOGW(TAG, "crash restart %u of %d before falling back to stock", fails, BOOT_FAIL_LIMIT);

    esp_timer_create_args_t ta = {};
    ta.callback = [](void *) { set_boot_fails(0); };
    ta.name = "bootok";
    esp_timer_handle_t t;
    if (esp_timer_create(&ta, &t) == ESP_OK) esp_timer_start_once(t, BOOT_HEALTHY_S * 1000000LL);
}
#endif

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
    if (monitor::last_emergency()[0])
        ESP_LOGW(TAG, "last emergency (%lu since power-on): %s", (unsigned long) monitor::emergency_count(), monitor::last_emergency());
    esp_err_t err = nvs_flash_init();
#if CONFIG_AF_BOARD_SUPRAHEX
    // This NVS also holds AxeOS's settings and the board identity: never erase it.
    if (err != ESP_OK) fall_back_to_stock("settings storage unreadable");
    boot_guard(rr);
#elif CONFIG_AF_BOARD_NERDAXE
    // shares NVS with the stock firmware's settings: never erase it
    if (err != ESP_OK) ESP_LOGE(TAG, "NVS init failed (%s), running on defaults", esp_err_to_name(err));
#else
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
#endif
    settings::load();
    Settings &cfg = settings::get();
    layout_store::load();

#if AF_HAS_LCD
    display::start();
#endif

    board::Error e = board::init_hardware();
#if !AF_HAS_LCD
    display::start();  // the OLED shares the I2C bus that init_hardware() brings up
#endif
    monitor::start();  // telemetry + fans + emergency cut-off, before any power-up
    wifi::start();
    captive::start();
    swarm::start();
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
