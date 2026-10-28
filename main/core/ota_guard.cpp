#include "ota_guard.h"

#include <string.h>
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#include "core/board.h"
#include "core/settings.h"
#include "core/state.h"
#include "net/notify.h"
#include "net/wifi.h"

static const char *TAG = "ota_guard";

#define NS "af_boot"     // the boot guard's namespace, outside the settings
#define CRASH_LIMIT 2    // crash restarts of a new image before going back
#define HEALTHY_S 120    // this long at a normal hashrate keeps the update
#define VERDICT_S 600    // online but not mining this long after boot: go back
#define MIN_RATE 0.5f    // of the expected hashrate

static bool s_pending = false;
static char s_prev[17] = "";

static void clear()
{
    s_pending = false;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_erase_key(h, "ota_prev");
    nvs_erase_key(h, "ota_sha");
    nvs_erase_key(h, "ota_tries");
    nvs_commit(h);
    nvs_close(h);
}

static void roll_back(const char *why)
{
    const esp_partition_t *prev = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, s_prev);
    esp_app_desc_t d;
    clear();
    if (!prev || prev == esp_ota_get_running_partition() || esp_ota_get_partition_description(prev, &d) != ESP_OK) {
        ESP_LOGE(TAG, "%s, but the previous firmware in '%s' is gone: keeping this one", why, s_prev);
        return;
    }
    ESP_LOGE(TAG, "%s: going back to %s %s in '%s'", why, d.project_name, d.version, prev->label);
    if (esp_ota_set_boot_partition(prev) != ESP_OK) return;
    notify::post(NOTIFY_FAULT, "update rolled back to %s (%s)", d.version, why);
    if (board::is_powered()) board::shutdown();
    vTaskDelay(pdMS_TO_TICKS(4000));  // give the message a moment to go out
    esp_restart();
}

namespace ota_guard {

void boot(esp_reset_reason_t rr)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t n = sizeof(s_prev);
    uint8_t sha[32];
    size_t sn = sizeof(sha);
    uint8_t tries = 0;
    bool armed = nvs_get_str(h, "ota_prev", s_prev, &n) == ESP_OK && nvs_get_blob(h, "ota_sha", sha, &sn) == ESP_OK;
    nvs_get_u8(h, "ota_tries", &tries);
    nvs_close(h);
    if (!armed) return;
    // a USB flash or another update since: this isn't the image that was armed
    if (sn != 32 || memcmp(sha, esp_app_get_description()->app_elf_sha256, 32) != 0 || !settings::get().ota_guard) {
        clear();
        return;
    }
    s_pending = true;
    bool crash = rr == ESP_RST_PANIC || rr == ESP_RST_INT_WDT || rr == ESP_RST_TASK_WDT || rr == ESP_RST_WDT;
    if (!crash) {
        ESP_LOGW(TAG, "new firmware on probation: kept once it mines normally for %d s", HEALTHY_S);
        return;
    }
    tries++;
    ESP_LOGW(TAG, "new firmware crashed at start-up (%u of %d)", tries, CRASH_LIMIT);
    if (tries >= CRASH_LIMIT) {
        roll_back("crashed at start-up twice");
        return;
    }
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "ota_tries", tries);
        nvs_commit(h);
        nvs_close(h);
    }
}

void armed(const esp_partition_t *updated)
{
    esp_app_desc_t d;
    const esp_partition_t *run = esp_ota_get_running_partition();
    if (!settings::get().ota_guard || !run || esp_ota_get_partition_description(updated, &d) != ESP_OK) return;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "ota_prev", run->label);
    nvs_set_blob(h, "ota_sha", d.app_elf_sha256, 32);
    nvs_set_u8(h, "ota_tries", 0);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "update %s armed; '%s' stays as the fallback", d.version, run->label);
}

void tick()
{
    static uint32_t good_s = 0;
    if (!s_pending) return;
    if (!settings::get().ota_guard) {
        clear();
        return;
    }
    const MinerState &s = g_state;
    bool hashing = board::is_powered() && s.pool_connected && s.expected_ghs > 0 &&
                   s.hashrate_ghs >= MIN_RATE * s.expected_ghs;
    good_s = hashing ? good_s + 1 : 0;
    if (good_s >= HEALTHY_S) {
        ESP_LOGI(TAG, "new firmware mines normally: keeping it");
        clear();
        return;
    }
    if (s.uptime_s < VERDICT_S || s.status == SysStatus::Paused || s.status == SysStatus::Overheat) return;
    if (s.status == SysStatus::Fault) roll_back("hardware fault after the update");
    else if (wifi::connected() && s.pool_connected && !hashing) roll_back("not mining 10 minutes after the update");
}

bool pending() { return s_pending; }

void keep()
{
    if (s_pending) ESP_LOGI(TAG, "kept by the user");
    clear();
}

}
