#include "swarm.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs.h"

#include "core/board_config.h"
#include "core/settings.h"
#include "core/state.h"
#include "net/http_util.h"
#include "net/wifi.h"

static const char *TAG = "swarm";

#define MAX_PEERS 24
#define POLL_S 15
#define HTTP_TIMEOUT_MS 2500
#define MAX_BODY 24576
// lwIP has 16 sockets in all (CONFIG_LWIP_MAX_SOCKETS), shared with the web
// server and pools; probing more at once starves them and the fetches below.
#define SCAN_BATCH 8
#define SCAN_CONNECT_MS 600
#define NVS_NS "af_swarm"

struct Peer {
    char ip[16];
    char name[33];
    char worker[33];    // pool worker name, the part after the address
    char kind[12];      // "AxeOS" (also NerdQAxe stock) or "AxeForge"
    char model[32];
    char version[24];
    float ghs, temp, power;
    uint32_t uptime_s;
    double best;
    uint32_t accepted, rejected;
    bool online;
    uint32_t seen_s;    // our uptime when it last answered
};

static Peer s_peers[MAX_PEERS];
static int s_count = 0;
static SemaphoreHandle_t s_lock;
static volatile bool s_scan = false, s_scanning = false;
static TaskHandle_t s_task;

namespace {
struct Lock {
    Lock() { xSemaphoreTake(s_lock, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(s_lock); }
};
}

// Strict dotted quad. inet_aton also takes short and octal forms and ignores
// anything after a space, which then breaks the URL built from it.
static bool valid_ip(const char *ip)
{
    if (!ip || strlen(ip) > 15) return false;
    int parts = 0, val = -1;
    for (const char *p = ip;; p++) {
        if (*p >= '0' && *p <= '9') {
            if (val == 0) return false;  // leading zero: inet_aton would read it as octal
            val = (val < 0 ? 0 : val * 10) + (*p - '0');
            if (val > 255) return false;
        } else if (*p == '.' || *p == 0) {
            if (val < 0) return false;
            parts++;
            val = -1;
            if (*p == 0) break;
        } else {
            return false;
        }
    }
    in_addr a;
    return parts == 4 && inet_aton(ip, &a) && a.s_addr != 0;
}

// ------------------------------------------------------------------ storage
static void save()
{
    char buf[MAX_PEERS * 16 + 1] = "";
    {
        Lock l;
        for (int i = 0; i < s_count; i++) {
            if (i) strcat(buf, ",");
            strcat(buf, s_peers[i].ip);
        }
    }
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "ips", buf);
    nvs_commit(h);
    nvs_close(h);
}

static void load()
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    char buf[MAX_PEERS * 16 + 1] = "";
    size_t len = sizeof(buf);
    if (nvs_get_str(h, "ips", buf, &len) == ESP_OK) {
        for (char *t = strtok(buf, ","); t && s_count < MAX_PEERS; t = strtok(NULL, ",")) {
            if (!valid_ip(t)) continue;
            memset(&s_peers[s_count], 0, sizeof(Peer));
            strcpy(s_peers[s_count].ip, t);
            s_count++;
        }
    }
    nvs_close(h);
}

// ------------------------------------------------------------------ polling
static double num(cJSON *o, const char *k)
{
    cJSON *v = cJSON_GetObjectItem(o, k);
    if (cJSON_IsNumber(v)) return v->valuedouble;
    if (cJSON_IsString(v) && v->valuestring) {
        // older AxeOS reports best difficulty as "4.38G"
        char *end;
        double d = strtod(v->valuestring, &end);
        switch (*end) {
        case 'k': case 'K': d *= 1e3; break;
        case 'M': d *= 1e6; break;
        case 'G': d *= 1e9; break;
        case 'T': d *= 1e12; break;
        case 'P': d *= 1e15; break;
        }
        return d;
    }
    return 0;
}

static void str(cJSON *o, const char *k, char *dst, size_t len)
{
    cJSON *v = cJSON_GetObjectItem(o, k);
    if (cJSON_IsString(v) && v->valuestring && v->valuestring[0]) snprintf(dst, len, "%s", v->valuestring);
}

// AxeOS (Bitaxe, NerdQAxe stock) API. False if the device doesn't speak it.
static bool fetch_axeos(const char *ip, Peer &p)
{
    char url[48];
    snprintf(url, sizeof(url), "http://%s/api/system/info", ip);
    char *body = http::get(url, MAX_BODY, HTTP_TIMEOUT_MS, true);
    cJSON *j = body ? cJSON_Parse(body) : nullptr;
    free(body);
    if (j && cJSON_GetObjectItem(j, "hashRate")) {
        strcpy(p.kind, "AxeOS");
        str(j, "hostname", p.name, sizeof(p.name));
        char user[96] = "";
        str(j, "stratumUser", user, sizeof(user));
        settings::worker_of(user, p.worker, sizeof(p.worker));
        str(j, "deviceModel", p.model, sizeof(p.model));
        if (!p.model[0]) {
            // stock Bitaxe AxeOS has no deviceModel here, but a board number and chip
            char board[8] = "", chip[12] = "";
            str(j, "boardVersion", board, sizeof(board));
            str(j, "ASICModel", chip, sizeof(chip));
            if (board[0]) snprintf(p.model, sizeof(p.model), "Bitaxe %s (%s)", board, chip);
            else snprintf(p.model, sizeof(p.model), "%s", chip);
        }
        str(j, "version", p.version, sizeof(p.version));
        p.ghs = num(j, "hashRate");
        p.temp = num(j, "temp");
        p.power = num(j, "power");
        p.uptime_s = (uint32_t) num(j, "uptimeSeconds");
        p.best = num(j, "bestDiff");
        p.accepted = (uint32_t) num(j, "sharesAccepted");
        p.rejected = (uint32_t) num(j, "sharesRejected");
        cJSON_Delete(j);
        return true;
    }
    cJSON_Delete(j);
    return false;
}

// AxeForge API. False if the device doesn't speak it.
static bool fetch_axeforge(const char *ip, Peer &p)
{
    char url[48];
    snprintf(url, sizeof(url), "http://%s/api/status", ip);
    char *body = http::get(url, MAX_BODY, HTTP_TIMEOUT_MS, true);
    cJSON *j = body ? cJSON_Parse(body) : nullptr;
    free(body);
    if (j && cJSON_GetObjectItem(j, "hashrate") && cJSON_GetObjectItem(j, "board")) {
        strcpy(p.kind, "AxeForge");
        str(j, "hostname", p.name, sizeof(p.name));
        if (!p.name[0]) snprintf(p.name, sizeof(p.name), "axeforge-%s", strrchr(ip, '.') + 1);  // older AxeForge
        str(j, "worker", p.worker, sizeof(p.worker));
        str(j, "board", p.model, sizeof(p.model));
        str(j, "version", p.version, sizeof(p.version));
        p.ghs = num(j, "hashrate");
        p.temp = num(j, "chipTemp");
        p.power = num(j, "power");
        p.uptime_s = (uint32_t) num(j, "uptime");
        cJSON *pool = cJSON_GetObjectItem(j, "pool");
        if (pool) {
            p.best = num(pool, "bestDiff");
            p.accepted = (uint32_t) num(pool, "accepted");
            p.rejected = (uint32_t) num(pool, "rejected");
        }
        cJSON_Delete(j);
        return true;
    }
    cJSON_Delete(j);
    return false;
}

// Fills `p` from whichever API the device speaks. False if it isn't a miner.
// A peer already known as AxeForge is asked on its own API first: probing it with
// /api/system/info every poll left a 404 and two warnings in its log every 15 s.
static bool fetch(const char *ip, Peer &p, const char *known_kind = "")
{
    if (!strcmp(known_kind, "AxeForge")) return fetch_axeforge(ip, p) || fetch_axeos(ip, p);
    return fetch_axeos(ip, p) || fetch_axeforge(ip, p);
}

static void poll_all()
{
    char ips[MAX_PEERS][16], kinds[MAX_PEERS][12];
    int n;
    {
        Lock l;
        n = s_count;
        for (int i = 0; i < n; i++) {
            strcpy(ips[i], s_peers[i].ip);
            strcpy(kinds[i], s_peers[i].kind);
        }
    }
    for (int i = 0; i < n; i++) {
        Peer p = {};
        strcpy(p.ip, ips[i]);
        bool ok = fetch(ips[i], p, kinds[i]);
        Lock l;
        for (int k = 0; k < s_count; k++) {
            if (strcmp(s_peers[k].ip, ips[i]) != 0) continue;
            if (ok) {
                p.online = true;
                p.seen_s = g_state.uptime_s;
                s_peers[k] = p;
            } else {
                s_peers[k].online = false;  // keep the last known name/model
            }
        }
    }
}

// ------------------------------------------------------------------ scanning
// Non-blocking connects to port 80 across the /24, a batch at a time; only
// hosts with a web server are then asked whether they are miners.
static void scan()
{
    in_addr self;
    if (!inet_aton(g_state.ip, &self)) return;
    uint32_t base = ntohl(self.s_addr) & 0xFFFFFF00u;
    int found = 0;
    ESP_LOGI(TAG, "scanning %s/24", g_state.ip);
    int h = 1, stalls = 0;
    while (h < 255) {
        int fds[SCAN_BATCH];
        uint32_t hosts[SCAN_BATCH];
        int n = 0;
        for (; n < SCAN_BATCH && h < 255; h++) {
            uint32_t ip = base | h;
            if (htonl(ip) == self.s_addr) continue;
            int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (fd < 0) break;  // out of sockets: this host starts the next batch
            fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
            sockaddr_in a = {};
            a.sin_family = AF_INET;
            a.sin_port = htons(80);
            a.sin_addr.s_addr = htonl(ip);
            connect(fd, (sockaddr *) &a, sizeof(a));
            fds[n] = fd;
            hosts[n] = ip;
            n++;
        }
        if (n == 0) {
            // nothing free right now; wait for the web server or a pool to let go
            if (++stalls > 10) {
                ESP_LOGW(TAG, "scan stopped at .%d: no free sockets", h);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(SCAN_CONNECT_MS));
            continue;
        }
        stalls = 0;
        vTaskDelay(pdMS_TO_TICKS(SCAN_CONNECT_MS));
        // close the whole batch before fetching, so the fetches have sockets
        bool open[SCAN_BATCH];
        for (int i = 0; i < n; i++) {
            fd_set w;
            FD_ZERO(&w);
            FD_SET(fds[i], &w);
            timeval tv = {0, 0};
            int err = 0;
            socklen_t el = sizeof(err);
            open[i] = select(fds[i] + 1, NULL, &w, NULL, &tv) > 0 &&
                      getsockopt(fds[i], SOL_SOCKET, SO_ERROR, &err, &el) == 0 && err == 0;
            close(fds[i]);
        }
        for (int i = 0; i < n; i++) {
            if (!open[i]) continue;
            char ip[16];
            in_addr ia;
            ia.s_addr = htonl(hosts[i]);
            snprintf(ip, sizeof(ip), "%s", inet_ntoa(ia));
            Peer p = {};
            strcpy(p.ip, ip);
            if (fetch(ip, p) && swarm::add(ip)) {
                found++;
                ESP_LOGI(TAG, "found %s %s at %s", p.kind, p.model, ip);
            }
        }
    }
    ESP_LOGI(TAG, "scan done, %d new miner(s)", found);
}

static void swarm_task(void *)
{
    while (true) {
        if (wifi::connected() && g_state.ip[0]) {
            if (s_scan) {
                s_scan = false;
                s_scanning = true;
                scan();
                s_scanning = false;
            }
            poll_all();
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(POLL_S * 1000));
    }
}

// ------------------------------------------------------------------ API
static void add_peer_json(cJSON *arr, const Peer &p, bool self)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "ip", p.ip);
    cJSON_AddStringToObject(o, "name", p.name);
    cJSON_AddStringToObject(o, "worker", p.worker);
    cJSON_AddStringToObject(o, "kind", p.kind);
    cJSON_AddStringToObject(o, "model", p.model);
    cJSON_AddStringToObject(o, "version", p.version);
    cJSON_AddBoolToObject(o, "online", p.online);
    cJSON_AddBoolToObject(o, "self", self);
    cJSON_AddNumberToObject(o, "hashrate", p.ghs);
    cJSON_AddNumberToObject(o, "temp", p.temp);
    cJSON_AddNumberToObject(o, "power", p.power);
    cJSON_AddNumberToObject(o, "uptime", p.uptime_s);
    cJSON_AddNumberToObject(o, "best", p.best);
    cJSON_AddNumberToObject(o, "accepted", p.accepted);
    cJSON_AddNumberToObject(o, "rejected", p.rejected);
    // seconds since it last answered; -1 = not seen since this miner booted
    double ago = p.online ? 0 : p.seen_s ? (double) (g_state.uptime_s - p.seen_s) : -1.0;
    cJSON_AddNumberToObject(o, "ago", ago);
    cJSON_AddItemToArray(arr, o);
}

namespace swarm {

void start()
{
    s_lock = xSemaphoreCreateMutex();
    load();
    xTaskCreatePinnedToCore(swarm_task, "swarm", 6144, NULL, 2, &s_task, 0);
}

char *to_json()
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "scanning", s_scanning || s_scan);
    cJSON *arr = cJSON_AddArrayToObject(r, "miners");

    const MinerState &s = g_state;
    Peer me = {};
    snprintf(me.ip, sizeof(me.ip), "%s", s.ip);
    strcpy(me.name, "this miner");
    settings::own_worker(me.worker, sizeof(me.worker));
    strcpy(me.kind, "AxeForge");
    snprintf(me.model, sizeof(me.model), "%s", AF_BOARD_NAME);
    me.online = true;
    me.ghs = s.hashrate_ghs;
    me.temp = s.chip_temp_max;
    me.power = s.pin;
    me.uptime_s = s.uptime_s;
    me.best = s.best_diff;
    me.accepted = s.shares_accepted;
    me.rejected = s.shares_rejected;
    add_peer_json(arr, me, true);
    {
        Lock l;
        for (int i = 0; i < s_count; i++) add_peer_json(arr, s_peers[i], false);
    }
    char *out = cJSON_PrintUnformatted(r);
    cJSON_Delete(r);
    return out;
}

bool add(const char *ip)
{
    if (!valid_ip(ip) || strcmp(ip, g_state.ip) == 0) return false;
    {
        Lock l;
        for (int i = 0; i < s_count; i++)
            if (strcmp(s_peers[i].ip, ip) == 0) return false;
        if (s_count >= MAX_PEERS) return false;
        memset(&s_peers[s_count], 0, sizeof(Peer));
        strcpy(s_peers[s_count].ip, ip);
        s_count++;
    }
    save();
    if (s_task && !s_scanning) xTaskNotifyGive(s_task);  // poll the newcomer right away
    return true;
}

bool remove(const char *ip)
{
    bool hit = false;
    {
        Lock l;
        for (int i = 0; i < s_count; i++) {
            if (strcmp(s_peers[i].ip, ip) != 0) continue;
            memmove(&s_peers[i], &s_peers[i + 1], sizeof(Peer) * (s_count - i - 1));
            s_count--;
            hit = true;
            break;
        }
    }
    if (hit) save();
    return hit;
}

void request_scan()
{
    s_scan = true;
    if (s_task) xTaskNotifyGive(s_task);
}

} // namespace swarm
