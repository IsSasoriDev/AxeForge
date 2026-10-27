#include "wifi.h"

#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#include "core/settings.h"
#include "core/state.h"

static const char *TAG = "wifi";

static volatile bool s_connected = false;
static volatile bool s_setup = false;
static int s_retries = 0;
static esp_timer_handle_t s_reconnect_timer;

static void reconnect_cb(void *) { esp_wifi_connect(); }

// The name given to DHCP (and shown by Swarm): ours if set, else the one the
// stock firmware saved (so a flashed miner keeps its old name), else one made
// from the MAC.
static void pick_hostname()
{
    const char *mine = settings::get().hostname;
    char stock[33] = "";
    nvs_handle_t h;
    if (!mine[0] && nvs_open("main", NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(stock);
        if (nvs_get_str(h, "hostname", stock, &len) != ESP_OK) stock[0] = 0;
        nvs_close(h);
        char *dot = strchr(stock, '.');  // stock firmware saves it as "name.local"
        if (dot) *dot = 0;
    }
    if (mine[0] && settings::valid_hostname(mine)) {
        snprintf(g_state.hostname, sizeof(g_state.hostname), "%s", mine);
    } else if (settings::valid_hostname(stock)) {
        snprintf(g_state.hostname, sizeof(g_state.hostname), "%s", stock);
    } else {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(g_state.hostname, sizeof(g_state.hostname), "axeforge-%02x%02x", mac[4], mac[5]);
    }
}

static void start_setup_ap()
{
    if (s_setup) return;
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    wifi_config_t ap = {};
    snprintf((char *) ap.ap.ssid, sizeof(ap.ap.ssid), "AxeForge-%02X%02X", mac[4], mac[5]);
    ap.ap.ssid_len = strlen((char *) ap.ap.ssid);
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_OPEN;
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &ap);
    strncpy(g_state.ap_ssid, (char *) ap.ap.ssid, sizeof(g_state.ap_ssid) - 1);
    s_setup = true;
    g_state.status = SysStatus::SetupAP;
    ESP_LOGW(TAG, "setup AP %s up at 192.168.4.1", g_state.ap_ssid);
}

static void on_event(void *, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (settings::get().wifi_ssid[0]) esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        auto *ev = (wifi_event_sta_disconnected_t *) data;
        // the reason codes are esp_wifi_types.h's wifi_err_reason_t (201 = AP not found, ...)
        if (s_connected || s_retries % 8 == 0)
            ESP_LOGW(TAG, "disconnected, reason %d, rssi %d (retry %d)", ev->reason, ev->rssi, s_retries + 1);
        s_connected = false;
        g_state.ip[0] = 0;
        if (++s_retries == 8) start_setup_ap();  // keep retrying STA in the background
        esp_timer_stop(s_reconnect_timer);
        esp_timer_start_once(s_reconnect_timer, (s_retries < 8 ? 1000 : 10000) * 1000ULL);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto *ev = (ip_event_got_ip_t *) data;
        snprintf(g_state.ip, sizeof(g_state.ip), IPSTR, IP2STR(&ev->ip_info.ip));
        s_connected = true;
        s_retries = 0;
        if (s_setup) {
            esp_wifi_set_mode(WIFI_MODE_STA);
            s_setup = false;
        }
        ESP_LOGI(TAG, "connected, ip %s", g_state.ip);
    }
}

namespace wifi {

void start()
{
    esp_timer_create_args_t targs = {};
    targs.callback = reconnect_cb;
    targs.name = "wifi_rc";
    esp_timer_create(&targs, &s_reconnect_timer);
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    pick_hostname();
    esp_netif_set_hostname(sta_netif, g_state.hostname);
    ESP_LOGI(TAG, "hostname %s", g_state.hostname);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);

    Settings &s = settings::get();
    strncpy(g_state.ssid, s.wifi_ssid, sizeof(g_state.ssid) - 1);
    esp_wifi_set_mode(WIFI_MODE_STA);
    if (s.wifi_ssid[0]) {
        wifi_config_t sta = {};
        strncpy((char *) sta.sta.ssid, s.wifi_ssid, sizeof(sta.sta.ssid));
        strncpy((char *) sta.sta.password, s.wifi_pass, sizeof(sta.sta.password));
        sta.sta.threshold.authmode = s.wifi_pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
        sta.sta.pmf_cfg.capable = true;
        esp_wifi_set_config(WIFI_IF_STA, &sta);
        g_state.status = SysStatus::Connecting;
    }
    esp_wifi_start();
    esp_wifi_set_ps(WIFI_PS_NONE);
    if (!s.wifi_ssid[0]) start_setup_ap();
}

bool connected() { return s_connected; }
bool in_setup_mode() { return s_setup; }

} // namespace wifi
