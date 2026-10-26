#include "pools.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_transport.h"
#include "esp_transport_ssl.h"
#include "esp_transport_tcp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

// Part of tcp_transport, but only declared in its private header in IDF 5.3.
extern "C" int esp_transport_get_socket(esp_transport_handle_t t);

extern "C" {
#include "libbase58.h"
#include "sv2_noise.h"
#include "sv2_protocol.h"
}

#include "core/settings.h"
#include "core/state.h"
#include "mining/miner.h"
#include "net/wifi.h"

static const char *TAG = "pools";

#define V1_RX_BUF 8192
#define SV2_RX_BUF 16384
#define ID_CONFIGURE 1
#define ID_SUBSCRIBE 2
#define ID_AUTHORIZE 3
#define ID_SUGGEST 4
#define ID_SUBMIT_BASE 100
#define SUGGEST_DIFF 1024
#define DEFAULT_VERSION_MASK 0x1fffe000
// Pools send new work every 30-60 s, so three silent minutes means the pool has
// stopped serving us; pools tend to write a silent miner off not long after.
#define IDLE_RECONNECT_S 180
#define HASHRATE_WINDOW_S 600

struct Pool {
    int idx = 0;
    TaskHandle_t task = nullptr;
    volatile bool restart = false;
    SemaphoreHandle_t lock = nullptr;     // job template
    SemaphoreHandle_t io = nullptr;       // transport writes (and V1 reads)
    esp_transport_handle_t t = nullptr;
    sv2_noise_ctx_t *noise = nullptr;
    PoolProto proto = PoolProto::V1;

    StratumJob job;
    bool have_job = false;
    uint32_t gen = 0;
    bool clean = false;
    char en1[67] = "";
    int en2_len = 4;
    uint32_t version_mask = DEFAULT_VERSION_MASK;
    uint32_t diff = SUGGEST_DIFF;
    uint32_t submit_id = ID_SUBMIT_BASE;
    int64_t submit_t_us = 0;

    // SV2 channel
    uint32_t channel_id = 0;
    uint32_t group_id = 0;
    bool channel_closed = false;  // pool sent CloseChannel: reconnect
    uint32_t seq = 0;
    uint8_t ext_prefix[32] = {};
    uint8_t ext_prefix_len = 0;
    uint8_t ext_size = 0;
    uint8_t prev_hash[32] = {};
    uint32_t prev_nbits = 0;
    bool has_prev = false;
    sv2_ext_job_t *pending[SV2_PENDING_JOBS_SIZE] = {};

    // hashrate estimate from accepted work
    double work_window = 0;
    int64_t window_start_us = 0;
};

static Pool s_pools[AF_MAX_POOLS];

static char *dup_str(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = (char *) malloc(n);
    memcpy(d, s, n);
    return d;
}

static void swap_words(const uint8_t *in, uint8_t *out)
{
    for (int i = 0; i < 32; i += 4) {
        out[i] = in[i + 3];
        out[i + 1] = in[i + 2];
        out[i + 2] = in[i + 1];
        out[i + 3] = in[i];
    }
}

static void set_status(Pool &p, const char *fmt, const char *arg = "")
{
    snprintf(g_state.pools[p.idx].status, sizeof(g_state.pools[p.idx].status), fmt, arg);
}

// Publishes a new template and wakes the job task on clean jobs.
static void publish(Pool &p, StratumJob &j, const char *coinb1_hex, const char *coinb2_hex)
{
    xSemaphoreTake(p.lock, portMAX_DELAY);
    free(p.job.coinb1);
    free(p.job.coinb2);
    p.job = j;
    p.job.coinb1 = dup_str(coinb1_hex);
    p.job.coinb2 = dup_str(coinb2_hex);
    p.have_job = true;
    p.gen++;
    if (j.clean) p.clean = true;
    xSemaphoreGive(p.lock);
    g_state.pools[p.idx].diff = p.diff;
    if (j.clean) miner::kick();
}

static void on_share_result(Pool &p, bool ok, const char *err)
{
    PoolState &ps = g_state.pools[p.idx];
    if (p.submit_t_us) ps.ping_ms = (uint32_t) ((esp_timer_get_time() - p.submit_t_us) / 1000);
    if (ok) {
        ps.accepted++;
        g_state.shares_accepted++;
        ps.last_share_s = g_state.uptime_s;
        ps.accepted_work += p.diff;
        int64_t now = esp_timer_get_time();
        if (!p.window_start_us) p.window_start_us = now;
        p.work_window += p.diff;
        double secs = (now - p.window_start_us) / 1e6;
        if (secs > 30) ps.ghs = (float) (p.work_window * 4294967296.0 / secs / 1e9);
        if (secs > HASHRATE_WINDOW_S) {  // slide: keep half the history
            p.work_window /= 2;
            p.window_start_us = now - (int64_t) (secs / 2 * 1e6);
        }
    } else {
        ps.rejected++;
        g_state.shares_rejected++;
        ESP_LOGW(TAG, "[%d] share rejected: %s", p.idx, err ? err : "?");
    }
}

// ------------------------------------------------------------------ transport
static bool transport_connect(Pool &p, const PoolCfg &cfg)
{
    p.t = (cfg.proto == PoolProto::V1Tls) ? esp_transport_ssl_init() : esp_transport_tcp_init();
    if (cfg.proto == PoolProto::V1Tls) esp_transport_ssl_crt_bundle_attach(p.t, esp_crt_bundle_attach);
    if (esp_transport_connect(p.t, cfg.url, cfg.port, 10000) < 0) {
        set_status(p, "Can't connect to %s", cfg.url);
        ESP_LOGE(TAG, "[%d] connect %s:%u failed", p.idx, cfg.url, cfg.port);
        esp_transport_destroy(p.t);
        p.t = nullptr;
        return false;
    }
    // Shares are small writes; with Nagle on they can sit waiting for the pool's
    // delayed ACK (tens of ms). Covers V1 as well as SV2.
    int fd = esp_transport_get_socket(p.t);
    int one = 1;
    if (fd < 0 || setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) != 0)
        ESP_LOGW(TAG, "[%d] could not set TCP_NODELAY", p.idx);
    return true;
}

static void transport_close(Pool &p)
{
    xSemaphoreTake(p.io, portMAX_DELAY);
    if (p.noise) {
        sv2_noise_destroy(p.noise);
        p.noise = nullptr;
    }
    if (p.t) {
        esp_transport_close(p.t);
        esp_transport_destroy(p.t);
        p.t = nullptr;
    }
    xSemaphoreGive(p.io);
    for (auto &j : p.pending) {
        if (j) sv2_ext_job_free(j);
        j = nullptr;
    }
    p.has_prev = false;
}

static bool send_line(Pool &p, const char *line)
{
    xSemaphoreTake(p.io, portMAX_DELAY);
    bool ok = p.t && esp_transport_write(p.t, line, strlen(line), 5000) == (int) strlen(line);
    xSemaphoreGive(p.io);
    return ok;
}

static bool send_frame(Pool &p, const uint8_t *frame, int len)
{
    xSemaphoreTake(p.io, portMAX_DELAY);
    bool ok = p.noise && p.t && sv2_noise_send(p.noise, p.t, frame, len) == 0;
    xSemaphoreGive(p.io);
    return ok;
}

// ------------------------------------------------------------------ Stratum V1
static void v1_notify(Pool &p, cJSON *params)
{
    if (!cJSON_IsArray(params) || cJSON_GetArraySize(params) < 9) return;
    const char *f[8];
    for (int i = 0; i < 8; i++) {
        if (i == 4) continue;
        cJSON *it = cJSON_GetArrayItem(params, i);
        if (!cJSON_IsString(it)) return;
        f[i] = it->valuestring;
    }
    cJSON *branches = cJSON_GetArrayItem(params, 4);
    if (!cJSON_IsArray(branches)) return;

    StratumJob j;
    strncpy(j.job_id, f[0], sizeof(j.job_id) - 1);
    work::hex2bin(f[1], j.prev_hash_raw, 32);
    j.n_merkle = cJSON_GetArraySize(branches);
    if (j.n_merkle > MAX_MERKLE_BRANCHES) return;
    for (int i = 0; i < j.n_merkle; i++) {
        cJSON *b = cJSON_GetArrayItem(branches, i);
        if (!cJSON_IsString(b)) return;
        work::hex2bin(b->valuestring, j.merkle[i], 32);
    }
    j.version = strtoul(f[5], NULL, 16);
    j.nbits = strtoul(f[6], NULL, 16);
    j.ntime = strtoul(f[7], NULL, 16);
    j.clean = cJSON_IsTrue(cJSON_GetArrayItem(params, 8));
    publish(p, j, f[2], f[3]);
    ESP_LOGI(TAG, "[%d] job %s clean=%d", p.idx, j.job_id, j.clean);
}

static void v1_message(Pool &p, const char *line)
{
    cJSON *root = cJSON_Parse(line);
    if (!root) return;
    cJSON *method = cJSON_GetObjectItem(root, "method");
    cJSON *params = cJSON_GetObjectItem(root, "params");
    cJSON *id = cJSON_GetObjectItem(root, "id");
    cJSON *result = cJSON_GetObjectItem(root, "result");

    if (cJSON_IsString(method)) {
        const char *m = method->valuestring;
        if (!strcmp(m, "mining.notify")) {
            v1_notify(p, params);
        } else if (!strcmp(m, "mining.set_difficulty")) {
            cJSON *d = cJSON_GetArrayItem(params, 0);
            if (cJSON_IsNumber(d)) {
                p.diff = d->valuedouble < 1 ? 1 : (uint32_t) d->valuedouble;
                g_state.pools[p.idx].diff = p.diff;
            }
        } else if (!strcmp(m, "mining.set_version_mask")) {
            cJSON *v = cJSON_GetArrayItem(params, 0);
            if (cJSON_IsString(v)) p.version_mask = strtoul(v->valuestring, NULL, 16);
        }
    } else if (cJSON_IsNumber(id)) {
        int rid = id->valueint;
        if (rid == ID_CONFIGURE && cJSON_IsObject(result)) {
            cJSON *mask = cJSON_GetObjectItem(result, "version-rolling.mask");
            if (cJSON_IsString(mask)) p.version_mask = strtoul(mask->valuestring, NULL, 16);
        } else if (rid == ID_SUBSCRIBE && cJSON_IsArray(result)) {
            cJSON *en1 = cJSON_GetArrayItem(result, 1);
            cJSON *en2 = cJSON_GetArrayItem(result, 2);
            if (cJSON_IsString(en1) && cJSON_IsNumber(en2)) {
                xSemaphoreTake(p.lock, portMAX_DELAY);
                strncpy(p.en1, en1->valuestring, sizeof(p.en1) - 1);
                p.en2_len = en2->valueint;
                xSemaphoreGive(p.lock);
            }
        } else if (rid == ID_AUTHORIZE) {
            if (!cJSON_IsTrue(result)) set_status(p, "Worker rejected - check address");
        } else if (rid >= ID_SUBMIT_BASE) {
            char *err = cJSON_IsTrue(result) ? nullptr : cJSON_PrintUnformatted(cJSON_GetObjectItem(root, "error"));
            on_share_result(p, cJSON_IsTrue(result), err);
            free(err);
        }
    }
    cJSON_Delete(root);
}

static void run_v1(Pool &p, const PoolCfg &cfg)
{
    char line[512];
    const esp_app_desc_t *app = esp_app_get_description();
    snprintf(line, sizeof(line),
             "{\"id\":%d,\"method\":\"mining.configure\",\"params\":[[\"version-rolling\"],"
             "{\"version-rolling.mask\":\"ffffffff\"}]}\n"
             "{\"id\":%d,\"method\":\"mining.subscribe\",\"params\":[\"AxeForge/%s\"]}\n",
             ID_CONFIGURE, ID_SUBSCRIBE, app->version);
    send_line(p, line);
    snprintf(line, sizeof(line),
             "{\"id\":%d,\"method\":\"mining.authorize\",\"params\":[\"%s\",\"%s\"]}\n"
             "{\"id\":%d,\"method\":\"mining.suggest_difficulty\",\"params\":[%d]}\n",
             ID_AUTHORIZE, cfg.user, cfg.pass, ID_SUGGEST, SUGGEST_DIFF);
    send_line(p, line);
    g_state.pools[p.idx].connected = true;
    set_status(p, cfg.proto == PoolProto::V1Tls ? "Connected (TLS)" : "Connected");

    char *buf = (char *) malloc(V1_RX_BUF);
    int fill = 0;
    int64_t last_rx = esp_timer_get_time();
    while (!p.restart) {
        // poll without the lock so submits are never blocked behind a read
        int ready = esp_transport_poll_read(p.t, 200);
        if (ready < 0) break;
        if (ready == 0) {
            if (esp_timer_get_time() - last_rx > IDLE_RECONNECT_S * 1000000LL) {
                ESP_LOGW(TAG, "[%d] idle too long, reconnecting", p.idx);
                break;
            }
            continue;
        }
        xSemaphoreTake(p.io, portMAX_DELAY);
        int n = esp_transport_read(p.t, buf + fill, V1_RX_BUF - 1 - fill, 100);
        xSemaphoreGive(p.io);
        if (n < 0 || (n == 0 && ready > 0 && cfg.proto == PoolProto::V1)) {
            set_status(p, "Connection closed");
            break;
        }
        if (n == 0) continue;
        last_rx = esp_timer_get_time();
        fill += n;
        buf[fill] = 0;
        char *start = buf, *nl;
        while ((nl = strchr(start, '\n')) != NULL) {
            *nl = 0;
            if (nl > start) v1_message(p, start);
            start = nl + 1;
        }
        fill = strlen(start);
        memmove(buf, start, fill + 1);
        if (fill >= V1_RX_BUF - 1) fill = 0;
    }
    free(buf);
}

// ------------------------------------------------------------------ Stratum V2
static bool decode_authority_key(const char *b58, uint8_t out[32])
{
    uint8_t decoded[64];
    size_t len = sizeof(decoded);
    if (!b58tobin(decoded, &len, b58, 0) || len != 38) return false;
    uint8_t *d = decoded + (sizeof(decoded) - len);  // b58tobin right-aligns
    if (d[0] != 0x01 || d[1] != 0x00) return false;
    memcpy(out, d + 2, 32);
    return true;
}

static void v2_publish_ext(Pool &p, sv2_ext_job_t *ej)
{
    StratumJob j;
    snprintf(j.job_id, sizeof(j.job_id), "%lu", (unsigned long) ej->job_id);
    swap_words(ej->prev_hash, j.prev_hash_raw);  // template expects the V1 wire order
    j.n_merkle = ej->merkle_path_count > MAX_MERKLE_BRANCHES ? MAX_MERKLE_BRANCHES : ej->merkle_path_count;
    memcpy(j.merkle, ej->merkle_path, j.n_merkle * 32);
    j.version = ej->version;
    j.nbits = ej->nbits;
    j.ntime = ej->ntime;
    j.clean = ej->clean_jobs;

    // coinb1 = prefix + extranonce prefix; the miner rolls ext_size bytes after it
    size_t l1 = ej->coinbase_prefix_len + ej->extranonce_prefix_len;
    char *h1 = (char *) malloc(l1 * 2 + 1);
    char *h2 = (char *) malloc(ej->coinbase_suffix_len * 2 + 1);
    work::bin2hex(ej->coinbase_prefix, ej->coinbase_prefix_len, h1);
    work::bin2hex(ej->extranonce_prefix, ej->extranonce_prefix_len, h1 + ej->coinbase_prefix_len * 2);
    work::bin2hex(ej->coinbase_suffix, ej->coinbase_suffix_len, h2);
    xSemaphoreTake(p.lock, portMAX_DELAY);
    p.en1[0] = 0;
    p.en2_len = p.ext_size;
    xSemaphoreGive(p.lock);
    publish(p, j, h1, h2);
    free(h1);
    free(h2);
    ESP_LOGI(TAG, "[%d] SV2 job %lu", p.idx, (unsigned long) ej->job_id);
}

static void v2_activate(Pool &p, sv2_ext_job_t *ej)
{
    memcpy(ej->prev_hash, p.prev_hash, 32);
    ej->nbits = p.prev_nbits;
    ej->clean_jobs = true;
    v2_publish_ext(p, ej);
    sv2_ext_job_free(ej);
}

static void v2_message(Pool &p, uint8_t type, const uint8_t *pl, int len)
{
    uint32_t channel;
    switch (type) {
    case SV2_MSG_NEW_EXTENDED_MINING_JOB: {
        sv2_ext_job_t *ej = sv2_parse_new_extended_mining_job(pl, len, &channel);
        if (!ej) return;
        if (!ej->version_rolling_allowed) {
            set_status(p, "Pool forbids version rolling");
            sv2_ext_job_free(ej);
            p.restart = true;
            return;
        }
        ej->extranonce_prefix_len = p.ext_prefix_len;
        memcpy(ej->extranonce_prefix, p.ext_prefix, p.ext_prefix_len);
        if (ej->ntime > 0 && p.has_prev) {
            v2_activate(p, ej);  // active now
        } else {
            int slot = ej->job_id % SV2_PENDING_JOBS_SIZE;  // future job
            if (p.pending[slot]) sv2_ext_job_free(p.pending[slot]);
            p.pending[slot] = ej;
        }
        break;
    }
    case SV2_MSG_SET_NEW_PREV_HASH: {
        uint32_t job_id, ntime, nbits;
        uint8_t prev[32];
        if (sv2_parse_set_new_prev_hash(pl, len, &channel, &job_id, prev, &ntime, &nbits) != 0) return;
        memcpy(p.prev_hash, prev, 32);
        p.prev_nbits = nbits;
        p.has_prev = true;
        int slot = job_id % SV2_PENDING_JOBS_SIZE;
        sv2_ext_job_t *ej = p.pending[slot];
        if (ej && ej->job_id == job_id) {
            p.pending[slot] = nullptr;
            ej->ntime = ntime;
            v2_activate(p, ej);
        }
        break;
    }
    case SV2_MSG_SET_EXTRANONCE_PREFIX: {
        uint8_t pfx[32], plen;
        if (sv2_parse_set_extranonce_prefix(pl, len, &channel, pfx, &plen) == 0) {
            memcpy(p.ext_prefix, pfx, plen);
            p.ext_prefix_len = plen;
        }
        break;
    }
    case SV2_MSG_SET_TARGET: {
        uint8_t target[32];
        if (sv2_parse_set_target(pl, len, &channel, target) == 0) {
            p.diff = sv2_target_to_pdiff(target);
            g_state.pools[p.idx].diff = p.diff;
        }
        break;
    }
    case SV2_MSG_SUBMIT_SHARES_SUCCESS: {
        uint32_t count = 0;
        if (sv2_parse_submit_shares_success(pl, len, &channel, &count) == 0) {
            if (count > 1000) count = 1000;
            for (uint32_t i = 0; i < count; i++) on_share_result(p, true, nullptr);
        }
        break;
    }
    case SV2_MSG_SUBMIT_SHARES_ERROR: {
        uint32_t seq;
        char code[64] = "";
        sv2_parse_submit_shares_error(pl, len, &channel, &seq, code, sizeof(code));
        on_share_result(p, false, code);
        break;
    }
    case SV2_MSG_CLOSE_CHANNEL: {
        // The pool can end the channel and keep the connection open; without a
        // channel there is no new work, so treat it as a disconnect.
        char reason[64] = "";
        if (sv2_parse_close_channel(pl, len, &channel, reason, sizeof(reason)) != 0) break;
        if (channel != p.channel_id && (p.group_id == 0 || channel != p.group_id)) break;
        ESP_LOGW(TAG, "[%d] pool closed SV2 channel %lu: %s", p.idx, (unsigned long) channel, reason);
        set_status(p, "Channel closed by pool: %s", reason);
        p.channel_closed = true;
        break;
    }
    default:
        break;
    }
}

static bool v2_recv(Pool &p, uint8_t *hdr, uint8_t *buf, int *len)
{
    return sv2_noise_recv(p.noise, p.t, hdr, buf, SV2_RX_BUF, len) == 0;
}

static void run_v2(Pool &p, const PoolCfg &cfg)
{
    uint8_t key[32];
    bool have_key = cfg.sv2_key[0] && decode_authority_key(cfg.sv2_key, key);
    if (cfg.sv2_key[0] && !have_key) {
        set_status(p, "Invalid SV2 authority key");
        vTaskDelay(pdMS_TO_TICKS(30000));
        return;
    }
    p.noise = sv2_noise_create();
    if (!p.noise || sv2_noise_handshake(p.noise, p.t, have_key ? key : nullptr) != 0) {
        set_status(p, "SV2 handshake failed");
        return;
    }

    uint8_t *buf = (uint8_t *) malloc(SV2_RX_BUF);
    uint8_t hdr[6];
    int len = 0;
    sv2_frame_header_t fh;
    const esp_app_desc_t *app = esp_app_get_description();

    int n = sv2_build_setup_connection(buf, SV2_RX_BUF, cfg.url, cfg.port, "AxeForge", "BM1370", app->version, "",
                                       SV2_SETUP_FLAGS_REQUIRES_VERSION_ROLLING);
    if (n < 0 || !send_frame(p, buf, n) || !v2_recv(p, hdr, buf, &len)) goto out;
    sv2_parse_frame_header(hdr, &fh);
    if (fh.msg_type != SV2_MSG_SETUP_CONNECTION_SUCCESS) {
        set_status(p, "SV2 setup refused");
        goto out;
    }

    n = sv2_build_open_extended_mining_channel(buf, SV2_RX_BUF, 1, cfg.user, 5e12f, 6);
    if (n < 0 || !send_frame(p, buf, n) || !v2_recv(p, hdr, buf, &len)) goto out;
    sv2_parse_frame_header(hdr, &fh);
    if (fh.msg_type != SV2_MSG_OPEN_EXTENDED_MINING_CHANNEL_SUCCESS) {
        set_status(p, "SV2 channel refused - check worker");
        goto out;
    }
    {
        uint32_t req, group;
        uint16_t ext_size;
        uint8_t target[32];
        if (sv2_parse_open_extended_channel_success(buf, len, &req, &p.channel_id, target, &ext_size, p.ext_prefix,
                                                    &p.ext_prefix_len, &group) != 0)
            goto out;
        p.ext_size = (uint8_t) ext_size;
        p.diff = sv2_target_to_pdiff(target);
        p.seq = 0;
        p.group_id = group;
        p.channel_closed = false;
    }
    p.version_mask = DEFAULT_VERSION_MASK;
    g_state.pools[p.idx].connected = true;
    g_state.pools[p.idx].diff = p.diff;
    set_status(p, have_key ? "Connected (SV2, verified)" : "Connected (SV2, encrypted)");
    ESP_LOGI(TAG, "[%d] SV2 channel %lu open, diff %lu", p.idx, (unsigned long) p.channel_id, (unsigned long) p.diff);

    {
        // Same rule as V1: a pool that has sent nothing for this long has stopped
        // giving us work, even if the socket still looks open.
        int64_t last_rx = esp_timer_get_time();
        while (!p.restart && !p.channel_closed) {
            int ready = esp_transport_poll_read(p.t, 1000);
            if (ready < 0) break;
            if (ready == 0) {
                if (esp_timer_get_time() - last_rx > IDLE_RECONNECT_S * 1000000LL) {
                    ESP_LOGW(TAG, "[%d] idle too long, reconnecting", p.idx);
                    set_status(p, "Pool silent, reconnecting");
                    break;
                }
                continue;
            }
            if (!v2_recv(p, hdr, buf, &len)) {
                set_status(p, "Connection closed");
                break;
            }
            last_rx = esp_timer_get_time();
            sv2_parse_frame_header(hdr, &fh);
            v2_message(p, fh.msg_type, buf, len);
        }
    }
out:
    free(buf);
}

// ------------------------------------------------------------------ task
static void pool_task(void *arg)
{
    Pool &p = *(Pool *) arg;
    PoolState &ps = g_state.pools[p.idx];
    int backoff_s = 2;

    while (true) {
        p.restart = false;
        PoolCfg cfg = settings::get().pools[p.idx];  // copy: settings may change under us
        ps.enabled = cfg.enabled && cfg.url[0] && cfg.user[0];
        ps.proto = (uint8_t) cfg.proto;
        strncpy(ps.host, cfg.url, sizeof(ps.host) - 1);
        if (!ps.enabled) {
            set_status(p, cfg.enabled ? "Missing host or worker" : "Disabled");
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // woken by reconfigure()
            continue;
        }
        if (!wifi::connected()) {
            set_status(p, "Waiting for Wi-Fi");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        set_status(p, "Connecting...");
        p.proto = cfg.proto;
        p.diff = SUGGEST_DIFF;
        p.version_mask = DEFAULT_VERSION_MASK;
        if (transport_connect(p, cfg)) {
            backoff_s = 2;
            if (cfg.proto == PoolProto::V2) run_v2(p, cfg);
            else run_v1(p, cfg);
        }
        ps.connected = false;
        xSemaphoreTake(p.lock, portMAX_DELAY);
        p.have_job = false;
        xSemaphoreGive(p.lock);
        // results for this connection's jobs would be stale (or carry the old
        // extranonce) on the next one
        miner::forget_pool(p.idx);
        transport_close(p);
        if (!p.restart) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(backoff_s * 1000));
            if (backoff_s < 60) backoff_s *= 2;
        }
    }
}

namespace pools {

void start()
{
    for (int i = 0; i < AF_MAX_POOLS; i++) {
        Pool &p = s_pools[i];
        p.idx = i;
        p.lock = xSemaphoreCreateMutex();
        p.io = xSemaphoreCreateMutex();
        char name[12];
        snprintf(name, sizeof(name), "pool%d", i);
        // SV2 handshake (secp256k1) needs a deep stack; keep it in PSRAM so
        // internal RAM stays free for DMA. Pool tasks never touch flash.
        xTaskCreatePinnedToCoreWithCaps(pool_task, name, 12288, &p, 5, &p.task, tskNO_AFFINITY, MALLOC_CAP_SPIRAM);
    }
}

void reconfigure()
{
    for (auto &p : s_pools) {
        p.restart = true;
        if (p.task) xTaskNotifyGive(p.task);
    }
}

bool ready(int i)
{
    Pool &p = s_pools[i];
    return g_state.pools[i].connected && p.have_job && (p.proto == PoolProto::V2 || p.en1[0]);
}

bool fetch_job(int i, StratumJob &out, JobInfo &info, uint32_t *gen)
{
    Pool &p = s_pools[i];
    bool got = false;
    xSemaphoreTake(p.lock, portMAX_DELAY);
    if (p.have_job && p.gen != *gen) {
        out = p.job;
        out.coinb1 = dup_str(p.job.coinb1);
        out.coinb2 = dup_str(p.job.coinb2);
        strcpy(info.en1, p.en1);
        info.en2_len = p.en2_len;
        info.version_mask = p.version_mask;
        info.difficulty = p.diff;
        *gen = p.gen;
        got = true;
    }
    xSemaphoreGive(p.lock);
    return got;
}

uint32_t generation(int i) { return s_pools[i].gen; }

bool take_clean_flag(int i)
{
    Pool &p = s_pools[i];
    xSemaphoreTake(p.lock, portMAX_DELAY);
    bool c = p.clean;
    p.clean = false;
    xSemaphoreGive(p.lock);
    return c;
}

void submit(int i, const Work &w, uint32_t nonce, uint32_t version)
{
    Pool &p = s_pools[i];
    if (!g_state.pools[i].connected) return;
    p.submit_t_us = esp_timer_get_time();
    if (p.proto == PoolProto::V2) {
        uint8_t en2[32];
        size_t en2_len = work::hex2bin(w.extranonce2, en2, sizeof(en2));
        uint8_t frame[128];
        int n = sv2_build_submit_shares_extended(frame, sizeof(frame), p.channel_id, p.seq++,
                                                 strtoul(w.job_id, NULL, 10), nonce, w.ntime, version, en2,
                                                 (uint8_t) en2_len);
        if (n > 0) send_frame(p, frame, n);
        return;
    }
    char line[320];
    snprintf(line, sizeof(line),
             "{\"id\":%lu,\"method\":\"mining.submit\",\"params\":[\"%s\",\"%s\",\"%s\",\"%08lx\",\"%08lx\",\"%08lx\"]}\n",
             (unsigned long) p.submit_id++, settings::get().pools[i].user, w.job_id, w.extranonce2,
             (unsigned long) w.ntime, (unsigned long) nonce, (unsigned long) (version ^ w.version));
    send_line(p, line);
}

} // namespace pools
