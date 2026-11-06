#include "wifi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#include "core/settings.h"
#include "core/state.h"
#include "net/ble_setup.h"
#include "net/eth.h"

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

// A miner that has Wi-Fi saved only opens its setup network after this long
// without a connection (a router restart takes a minute or two), and then with a
// password: anyone in radio range can force a disconnect.
#define SETUP_AFTER_S 300
static int64_t s_down_since_us = 0;

// The setup network's password on a configured miner: random, made once and kept
// in NVS, shown on the screen. Never logged (the log is readable on the LAN).
static void setup_password(char *out, size_t len)
{
    static const char CHARS[] = "abcdefghjkmnpqrstuvwxyz23456789";  // nothing that reads as 0/o or 1/l
    const size_t want = 10;
    out[0] = 0;
    if (len <= want) return;
    nvs_handle_t h;
    if (nvs_open("af_boot", NVS_READWRITE, &h) != ESP_OK) return;
    size_t n = len;
    if (nvs_get_str(h, "appass", out, &n) != ESP_OK || strlen(out) != want) {
        for (size_t i = 0; i < want; i++) out[i] = CHARS[esp_random() % (sizeof(CHARS) - 1)];
        out[want] = 0;
        nvs_set_str(h, "appass", out);
        nvs_commit(h);
    }
    nvs_close(h);
}

static bool s_setup_starting = false;

static void setup_ap_task(void *);

// Called from the Wi-Fi and Ethernet event handlers, which run on the system
// event task (2.3 KB of stack). Bringing the network up, making the password
// and starting Bluetooth provisioning need far more than that, so the work is
// handed to a task of its own; done in the handler it overflowed the stack and
// the miner panicked every 5 minutes without ever showing the network.
static void start_setup_ap()
{
    if (s_setup || s_setup_starting || eth::up()) return;  // on a cable the miner is reachable already
    s_setup_starting = true;
    if (xTaskCreate(setup_ap_task, "setup_ap", 6144, NULL, 5, NULL) != pdPASS) {
        s_setup_starting = false;
        ESP_LOGE(TAG, "setup network: no memory for its task");
    }
}

static void setup_ap_task(void *)
{
    // first setup: open, there is nothing on the miner yet. Later: WPA2, and the
    // web server answers only the Wi-Fi form on this network (web.cpp)
    bool configured = settings::get().wifi_ssid[0] != 0;
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    wifi_config_t ap = {};
    snprintf((char *) ap.ap.ssid, sizeof(ap.ap.ssid), "AxeForge-%02X%02X", mac[4], mac[5]);
    ap.ap.ssid_len = strlen((char *) ap.ap.ssid);
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_OPEN;
    g_state.ap_pass[0] = 0;
    if (configured) setup_password(g_state.ap_pass, sizeof(g_state.ap_pass));
    if (g_state.ap_pass[0]) {
        snprintf((char *) ap.ap.password, sizeof(ap.ap.password), "%s", g_state.ap_pass);
        ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &ap);
    strncpy(g_state.ap_ssid, (char *) ap.ap.ssid, sizeof(g_state.ap_ssid) - 1);
    s_setup = true;
    g_state.status = SysStatus::SetupAP;
    ESP_LOGW(TAG, "setup AP %s up at 192.168.4.1 (%s)", g_state.ap_ssid,
             g_state.ap_pass[0] ? "WPA2, password on the screen" : "open");
    ble_setup::start(g_state.ap_pass[0] ? g_state.ap_pass : nullptr);
    s_setup_starting = false;
    vTaskDelete(NULL);
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
        snprintf(g_state.ip, sizeof(g_state.ip), "%s", eth::ip());
        if (s_retries == 0) s_down_since_us = esp_timer_get_time();
        ++s_retries;
        if (esp_timer_get_time() - s_down_since_us >= SETUP_AFTER_S * 1000000LL)
            start_setup_ap();  // keeps retrying STA in the background
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
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
            ESP_LOGI(TAG, "connected, ip %s, AP %02x:%02x:%02x:%02x:%02x:%02x channel %d, %d dBm", g_state.ip, ap.bssid[0],
                     ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5], ap.primary, ap.rssi);
        else
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
        // several access points may carry the same name (a mesh, an extender):
        // scan every channel and take the strongest, not the first to answer
        sta.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        sta.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
        esp_wifi_set_config(WIFI_IF_STA, &sta);
        g_state.status = SysStatus::Connecting;
    }
    esp_wifi_start();
    esp_wifi_set_ps(WIFI_PS_NONE);
    if (!s.wifi_ssid[0]) start_setup_ap();
}

bool connected() { return s_connected || eth::up(); }

void eth_changed()
{
    if (eth::up()) {
        if (!s_connected) snprintf(g_state.ip, sizeof(g_state.ip), "%s", eth::ip());
        if (s_setup && !ble_setup::active()) {
            esp_wifi_set_mode(WIFI_MODE_STA);
            s_setup = false;
            if (g_state.status == SysStatus::SetupAP) g_state.status = SysStatus::Connecting;
            ESP_LOGI(TAG, "setup network closed: reachable over Ethernet");
        }
    } else if (!s_connected) {
        g_state.ip[0] = 0;
        if (!settings::get().wifi_ssid[0]) start_setup_ap();
    }
}

cJSON *scan()
{
    cJSON *a = cJSON_CreateArray();
    wifi_scan_config_t sc = {};
    sc.show_hidden = false;
    if (esp_wifi_scan_start(&sc, true) != ESP_OK) return a;
    uint16_t n = 24;
    wifi_ap_record_t *recs = (wifi_ap_record_t *) calloc(n, sizeof(wifi_ap_record_t));
    if (!recs) return a;
    esp_wifi_scan_get_ap_records(&n, recs);  // sorted by signal
    for (int i = 0; i < n; i++) {
        if (!recs[i].ssid[0]) continue;
        bool dup = false;  // one entry per name (mesh systems repeat it)
        for (int k = 0; k < i && !dup; k++) dup = !strcmp((char *) recs[k].ssid, (char *) recs[i].ssid);
        if (dup) continue;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "ssid", (char *) recs[i].ssid);
        cJSON_AddNumberToObject(o, "rssi", recs[i].rssi);
        cJSON_AddBoolToObject(o, "secure", recs[i].authmode != WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(a, o);
    }
    free(recs);
    return a;
}
bool in_setup_mode() { return s_setup; }

} // namespace wifi
