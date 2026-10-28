#include "ble_setup.h"

#include "sdkconfig.h"

#if CONFIG_BT_ENABLED

#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"

#include "core/settings.h"
#include "core/state.h"

static const char *TAG = "ble";

static bool s_active = false, s_ended = false;
static char s_name[24] = "";

static void on_prov(void *, esp_event_base_t, int32_t id, void *data)
{
    switch (id) {
    case WIFI_PROV_CRED_RECV: {
        auto *sta = (wifi_sta_config_t *) data;
        Settings &c = settings::get();
        snprintf(c.wifi_ssid, sizeof(c.wifi_ssid), "%s", (const char *) sta->ssid);
        snprintf(c.wifi_pass, sizeof(c.wifi_pass), "%s", (const char *) sta->password);
        snprintf(g_state.ssid, sizeof(g_state.ssid), "%s", c.wifi_ssid);
        settings::save();
        ESP_LOGW(TAG, "Wi-Fi '%s' received over Bluetooth, connecting", c.wifi_ssid);
        break;
    }
    case WIFI_PROV_CRED_FAIL: {
        auto *r = (wifi_prov_sta_fail_reason_t *) data;
        ESP_LOGW(TAG, "connecting failed: %s", *r == WIFI_PROV_STA_AUTH_ERROR ? "wrong password" : "network not found");
        wifi_prov_mgr_reset_sm_state_on_failure();  // let the app try again
        break;
    }
    case WIFI_PROV_CRED_SUCCESS: ESP_LOGW(TAG, "Wi-Fi set up over Bluetooth"); break;
    case WIFI_PROV_END:
        // releases the BLE stack and, with FREE_BTDM, the controller's memory for good
        wifi_prov_mgr_deinit();
        s_active = false;
        s_ended = true;
        s_name[0] = 0;
        ESP_LOGI(TAG, "Bluetooth off");
        break;
    default: break;
    }
}

namespace ble_setup {

void start()
{
    if (s_active || s_ended || !settings::get().ble_setup) return;
    wifi_prov_mgr_config_t cfg = {};
    cfg.scheme = wifi_prov_scheme_ble;
    cfg.scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM;
    if (wifi_prov_mgr_init(cfg) != ESP_OK) {
        ESP_LOGE(TAG, "provisioning init failed");
        return;
    }
    esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, on_prov, NULL);
    // "no_pop": the app doesn't ask for a PIN; "wifi_scan": it lists networks
    static const char *caps[] = {"wifi_scan", "no_pop"};
    wifi_prov_mgr_set_app_info("axeforge", "v1", caps, 2);
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(s_name, sizeof(s_name), "PROV_AXEFORGE_%02X%02X", mac[4], mac[5]);
    wifi_prov_mgr_disable_auto_stop(1000);  // stop shortly after success, not right away
    esp_err_t err = wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, NULL, s_name, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "provisioning start failed: %s", esp_err_to_name(err));
        wifi_prov_mgr_deinit();
        s_name[0] = 0;
        return;
    }
    // the BLE scheme puts Wi-Fi in station mode: keep the setup network up as well
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    s_active = true;
    ESP_LOGW(TAG, "Bluetooth setup on: find %s in the ESP BLE Provisioning app", s_name);
}

bool active() { return s_active; }
const char *name() { return s_name; }

}

#else
namespace ble_setup {
void start() {}
bool active() { return false; }
const char *name() { return ""; }
}
#endif
