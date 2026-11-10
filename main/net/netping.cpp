#include "netping.h"

#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "ping/ping_sock.h"

#include "core/settings.h"
#include "core/state.h"
#include "net/eth.h"
#include "net/wifi.h"

static const char *TAG = "netping";

#define PING_INTERVAL_MS 5000
#define PING_TIMEOUT_MS 1000
#define LOSS_WINDOW 20  // loss is reported over the last this many pings

static esp_ping_handle_t s_ping = nullptr;
static uint8_t s_results[LOSS_WINDOW];  // 1 = answered, 0 = lost
static int s_count = 0, s_pos = 0;

static void record(bool ok, uint32_t ms)
{
    s_results[s_pos] = ok;
    s_pos = (s_pos + 1) % LOSS_WINDOW;
    if (s_count < LOSS_WINDOW) s_count++;
    int lost = 0;
    for (int i = 0; i < s_count; i++) lost += !s_results[i];
    g_state.net_loss_pct = 100.0f * lost / s_count;
    if (ok) g_state.net_rtt_ms = g_state.net_rtt_ms > 0 ? 0.7f * g_state.net_rtt_ms + 0.3f * ms : ms;
}

static void on_success(esp_ping_handle_t h, void *)
{
    uint32_t ms = 0;
    esp_ping_get_profile(h, ESP_PING_PROF_TIMEGAP, &ms, sizeof(ms));
    record(true, ms);
}

static void on_timeout(esp_ping_handle_t, void *) { record(false, 0); }

static void stop()
{
    if (!s_ping) return;
    esp_ping_stop(s_ping);
    esp_ping_delete_session(s_ping);
    s_ping = nullptr;
}

static bool start_session(const char *host)
{
    struct addrinfo hint = {}, *res = nullptr;
    hint.ai_family = AF_INET;
    if (getaddrinfo(host, nullptr, &hint, &res) != 0 || !res) {
        ESP_LOGW(TAG, "cannot resolve %s", host);
        return false;
    }
    ip_addr_t target = {};
    struct in_addr a4 = ((struct sockaddr_in *) res->ai_addr)->sin_addr;
    inet_addr_to_ip4addr(ip_2_ip4(&target), &a4);
    IP_SET_TYPE(&target, IPADDR_TYPE_V4);
    freeaddrinfo(res);

    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    cfg.target_addr = target;
    cfg.count = ESP_PING_COUNT_INFINITE;
    cfg.interval_ms = PING_INTERVAL_MS;
    cfg.timeout_ms = PING_TIMEOUT_MS;
    // the default 2 KB overflows when a failed send logs through logbuf's hook (the
    // line is formatted twice, for /api/logs and for the UART): offline, the miner
    // panicked about a minute after boot and never reached its setup network
    cfg.task_stack_size = 4096;
    esp_ping_callbacks_t cbs = {};
    cbs.on_ping_success = on_success;
    cbs.on_ping_timeout = on_timeout;
    if (esp_ping_new_session(&cfg, &cbs, &s_ping) != ESP_OK) {
        s_ping = nullptr;
        return false;
    }
    s_count = s_pos = 0;
    g_state.net_rtt_ms = 0;
    g_state.net_loss_pct = 0;
    esp_ping_start(s_ping);
    ESP_LOGI(TAG, "pinging %s every %d s", host, PING_INTERVAL_MS / 1000);
    return true;
}

// The pool doing the work: the first connected one, else the first enabled one.
static const char *pool_host()
{
    const Settings &cfg = settings::get();
    for (int i = 0; i < AF_POOLS; i++)
        if (g_state.pools[i].connected && cfg.pools[i].url[0]) return cfg.pools[i].url;
    for (int i = 0; i < AF_POOLS; i++)
        if (cfg.pools[i].enabled && cfg.pools[i].url[0]) return cfg.pools[i].url;
    return "";
}

static void ping_task(void *)
{
    char current[sizeof(g_state.net_target)] = "";
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        if (!wifi::connected() && !eth::up()) {  // nothing to ping over: every send would fail
            stop();
            current[0] = 0;
            continue;
        }
        const char *host = pool_host();
        if (!strcmp(host, current) && s_ping) continue;
        stop();
        snprintf(current, sizeof(current), "%s", host);
        snprintf(g_state.net_target, sizeof(g_state.net_target), "%s", host);
        if (current[0]) start_session(current);
    }
}

namespace netping {
void start() { xTaskCreate(ping_task, "netping", 3072, NULL, 3, NULL); }
}
