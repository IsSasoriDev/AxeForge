#include "notify.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

#include "core/settings.h"
#include "core/state.h"
#include "net/wifi.h"

static const char *TAG = "notify";

#define MSG_LEN 220
struct Msg {
    char text[MSG_LEN];
};
static QueueHandle_t s_q;

static bool send_json(const char *url, const char *body)
{
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.method = HTTP_METHOD_POST;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms = 10000;
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return false;
    esp_http_client_set_header(c, "Content-Type", "application/json");
    esp_http_client_set_post_field(c, body, strlen(body));
    esp_err_t err = esp_http_client_perform(c);
    int status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);
    if (err != ESP_OK || status < 200 || status >= 300) {
        // the URL holds the webhook secret / bot token: never log it
        ESP_LOGW(TAG, "send failed (%s, HTTP %d)", esp_err_to_name(err), status);
        return false;
    }
    return true;
}

static void deliver(const char *text)
{
    const Settings &s = settings::get();
    char line[MSG_LEN + 48];
    snprintf(line, sizeof(line), "%s: %s", g_state.hostname[0] ? g_state.hostname : "AxeForge", text);
    if (s.discord_url[0]) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "content", line);
        char *body = cJSON_PrintUnformatted(o);
        cJSON_Delete(o);
        send_json(s.discord_url, body);
        free(body);
    }
    if (s.tg_token[0] && s.tg_chat[0]) {
        char url[128];
        snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/sendMessage", s.tg_token);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "chat_id", s.tg_chat);
        cJSON_AddStringToObject(o, "text", line);
        char *body = cJSON_PrintUnformatted(o);
        cJSON_Delete(o);
        send_json(url, body);
        free(body);
    }
}

static void task(void *)
{
    bool boot_sent = false;
    uint32_t pool_down_since = 0;
    bool pool_down_sent = false;
    Msg m;
    while (true) {
        if (xQueueReceive(s_q, &m, pdMS_TO_TICKS(5000)) == pdTRUE) {
            if (wifi::connected()) deliver(m.text);
            vTaskDelay(pdMS_TO_TICKS(2000));  // Discord allows ~5 messages per 2 s; stay well under
            continue;
        }
        // events that are a state rather than a moment are watched here
        if (!wifi::connected()) continue;
        if (!boot_sent && g_state.uptime_s > 20) {
            boot_sent = true;
            notify::post(NOTIFY_BOOT, "started AxeForge %s (last reset: %s)", esp_app_get_description()->version,
                         g_state.reset_reason[0] ? g_state.reset_reason : "power on");
        }
        bool down = !g_state.pool_connected && g_state.status == SysStatus::Mining;
        if (down) {
            if (!pool_down_since) pool_down_since = g_state.uptime_s;
            if (!pool_down_sent && g_state.uptime_s - pool_down_since >= 300) {
                pool_down_sent = true;
                notify::post(NOTIFY_POOL, "no pool has given work for 5 minutes");
            }
        } else {
            if (pool_down_sent) notify::post(NOTIFY_POOL, "mining again, a pool is back");
            pool_down_since = 0;
            pool_down_sent = false;
        }
    }
}

namespace notify {

void start()
{
    s_q = xQueueCreate(8, sizeof(Msg));
    // HTTPS needs a big stack; internal RAM is short, and this task never writes flash
    xTaskCreateWithCaps(task, "notify", 8192, NULL, 3, NULL, MALLOC_CAP_SPIRAM);
}

void post(uint8_t event, const char *fmt, ...)
{
    const Settings &s = settings::get();
    if (!s_q || !(s.notify_events & event)) return;
    if (!s.discord_url[0] && !(s.tg_token[0] && s.tg_chat[0])) return;
    Msg m;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(m.text, sizeof(m.text), fmt, ap);
    va_end(ap);
    xQueueSend(s_q, &m, 0);  // a full queue drops the message rather than stall mining
}

bool test()
{
    const Settings &s = settings::get();
    if (!s_q || (!s.discord_url[0] && !(s.tg_token[0] && s.tg_chat[0]))) return false;
    Msg m;
    snprintf(m.text, sizeof(m.text), "test message. Notifications work.");
    xQueueSend(s_q, &m, 0);
    return true;
}

bool valid_tg_token(const char *t)
{
    // "123456789:AA..." : it goes into the URL path, so letters, digits, ':', '_' and '-' only
    size_t n = strlen(t);
    if (n < 20 || n >= sizeof(Settings::tg_token) || !strchr(t, ':')) return false;
    for (const char *p = t; *p; p++)
        if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == ':' ||
              *p == '_' || *p == '-'))
            return false;
    return true;
}

bool valid_discord(const char *url)
{
    for (const char *p = url; *p; p++)
        if (*p <= ' ' || *p == '"' || *p == '\\') return false;
    return !strncmp(url, "https://discord.com/api/webhooks/", 33) ||
           !strncmp(url, "https://discordapp.com/api/webhooks/", 36) ||
           !strncmp(url, "https://ptb.discord.com/api/webhooks/", 37) ||
           !strncmp(url, "https://canary.discord.com/api/webhooks/", 40);
}

}
