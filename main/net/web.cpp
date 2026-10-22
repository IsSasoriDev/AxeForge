#include "web.h"

#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "core/board.h"
#include "core/logbuf.h"
#include "core/settings.h"
#include "core/state.h"
#include "core/tuner.h"
#include "mining/miner.h"
#include "mining/work.h"
#include "net/mempool.h"
#include "net/weather.h"
#include "stratum/pools.h"
#include "ui/layout_store.h"
#include "ui/ui.h"
#include "ui/display.h"

static const char *TAG = "web";

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
    char *out = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr(req, out);
    free(out);
    cJSON_Delete(root);
    return ESP_OK;
}

// Reads the full request body (NUL terminated, heap). NULL if empty/too big.
static char *recv_body(httpd_req_t *req, size_t max)
{
    if (req->content_len <= 0 || req->content_len >= max) return nullptr;
    char *b = (char *) malloc(req->content_len + 1);
    int got = 0;
    while (got < req->content_len) {
        int n = httpd_req_recv(req, b + got, req->content_len - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) {
            free(b);
            return nullptr;
        }
        got += n;
    }
    b[got] = 0;
    return b;
}

static esp_err_t index_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, index_html_start, index_html_end - index_html_start - 1);
}

static const char *phase_name(TunerPhase p)
{
    switch (p) {
    case TunerPhase::Off: return "manual";
    case TunerPhase::WarmUp: return "warmup";
    case TunerPhase::Measuring: return "testing";
    case TunerPhase::StepUp: return "stepping";
    case TunerPhase::Locked: return "locked";
    case TunerPhase::Backoff: return "backoff";
    }
    return "";
}

static const char *status_name(SysStatus s)
{
    switch (s) {
    case SysStatus::Booting: return "booting";
    case SysStatus::NoWifi: return "no wifi";
    case SysStatus::SetupAP: return "setup";
    case SysStatus::Connecting: return "connecting";
    case SysStatus::Mining: return "mining";
    case SysStatus::Fault: return "fault";
    case SysStatus::Overheat: return "overheat";
    }
    return "";
}

// tiny SVG icon so browsers stop asking (and filling the log with 404s)
static esp_err_t favicon_get(httpd_req_t *req)
{
    static const char svg[] = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'><rect width='32' height='32' rx='7' fill='#070a10'/>"
                              "<path d='M8 24L16 6l8 18h-5l-3-7-3 7z' fill='#00e5ff'/><circle cx='24' cy='9' r='3' fill='#ff2bd6'/></svg>";
    httpd_resp_set_type(req, "image/svg+xml");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");
    return httpd_resp_send(req, svg, sizeof(svg) - 1);
}

static esp_err_t status_get(httpd_req_t *req)
{
    const MinerState &s = g_state;
    wifi_ap_record_t ap;
    int rssi = esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;

    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "status", status_name(s.status));
    cJSON_AddStringToObject(r, "fault", s.fault);
    cJSON_AddNumberToObject(r, "hashrate", s.hashrate_ghs);
    cJSON_AddNumberToObject(r, "expected", s.expected_ghs);
    cJSON_AddNumberToObject(r, "errorPct", s.error_pct);
    cJSON_AddNumberToObject(r, "chipTemp", s.chip_temp_max);
    cJSON_AddNumberToObject(r, "vrTemp", s.vr_temp);
    cJSON_AddNumberToObject(r, "power", s.pin);
    cJSON_AddNumberToObject(r, "vin", s.vin);
    cJSON_AddNumberToObject(r, "vout", s.vout);
    cJSON_AddNumberToObject(r, "iout", s.iout);
    cJSON_AddNumberToObject(r, "freq", s.freq_mhz);
    cJSON_AddNumberToObject(r, "coreMv", s.core_mv);
    cJSON_AddNumberToObject(r, "fanPct", s.fan_pct);
    cJSON_AddNumberToObject(r, "fanRpm", s.fan_rpm[0]);
    cJSON_AddNumberToObject(r, "fan2Rpm", s.fan_rpm[1]);
    cJSON_AddNumberToObject(r, "uptime", s.uptime_s);
    cJSON_AddStringToObject(r, "resetReason", s.reset_reason);
    cJSON_AddStringToObject(r, "screen", display::small() ? "tdisplay" : "big");
    cJSON_AddNumberToObject(r, "rssi", rssi);
    cJSON_AddStringToObject(r, "ip", s.ip);
    cJSON_AddStringToObject(r, "ssid", s.ssid);
    cJSON_AddStringToObject(r, "version", esp_app_get_description()->version);
    cJSON_AddNumberToObject(r, "freeHeap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(r, "freeInternal", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(r, "freeDma", heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    cJSON_AddNumberToObject(r, "chipsDetected", s.chips_detected);

    cJSON *pool = cJSON_AddObjectToObject(r, "pool");
    cJSON_AddBoolToObject(pool, "connected", s.pool_connected);
    cJSON_AddStringToObject(pool, "host", s.tmpl.valid ? s.pools[s.tmpl.pool < 0 ? 0 : s.tmpl.pool].host : "");
    cJSON_AddNumberToObject(pool, "diff", s.pool_diff);
    cJSON_AddNumberToObject(pool, "accepted", s.shares_accepted);
    cJSON_AddNumberToObject(pool, "rejected", s.shares_rejected);
    cJSON_AddNumberToObject(pool, "bestDiff", s.best_diff);
    cJSON_AddNumberToObject(pool, "sessionBest", s.session_best);
    cJSON_AddNumberToObject(pool, "height", s.block_height);
    cJSON_AddNumberToObject(pool, "networkDiff", work::nbits_to_difficulty(s.network_nbits));
    cJSON_AddNumberToObject(pool, "blocks", s.blocks_found);
    cJSON_AddStringToObject(pool, "mode", settings::get().pool_mode == PoolMode::Failover ? "failover" : "split");

    cJSON *pl = cJSON_AddArrayToObject(r, "pools");
    for (int i = 0; i < AF_POOLS; i++) {
        const PoolState &ps = s.pools[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "enabled", ps.enabled);
        cJSON_AddBoolToObject(o, "connected", ps.connected);
        cJSON_AddNumberToObject(o, "proto", ps.proto);
        cJSON_AddStringToObject(o, "host", ps.host);
        cJSON_AddNumberToObject(o, "diff", ps.diff);
        cJSON_AddNumberToObject(o, "accepted", ps.accepted);
        cJSON_AddNumberToObject(o, "rejected", ps.rejected);
        cJSON_AddNumberToObject(o, "best", ps.best);
        cJSON_AddNumberToObject(o, "ghs", ps.ghs);
        cJSON_AddNumberToObject(o, "workPct", ps.work_pct);
        cJSON_AddNumberToObject(o, "jobs", ps.jobs);
        cJSON_AddNumberToObject(o, "pingMs", ps.ping_ms);
        cJSON_AddStringToObject(o, "status", ps.status);
        cJSON_AddItemToArray(pl, o);
    }

    const BlockTemplate &bt = s.tmpl;
    cJSON *tm = cJSON_AddObjectToObject(r, "template");
    cJSON_AddBoolToObject(tm, "valid", bt.valid);
    cJSON_AddNumberToObject(tm, "pool", bt.pool);
    cJSON_AddNumberToObject(tm, "height", bt.height);
    cJSON_AddStringToObject(tm, "prevHash", bt.prev_hash);
    cJSON_AddNumberToObject(tm, "reward", bt.reward_btc);
    cJSON_AddNumberToObject(tm, "branches", bt.branches);
    cJSON_AddStringToObject(tm, "tag", bt.tag);
    cJSON_AddNumberToObject(tm, "ntime", bt.ntime);
    cJSON_AddStringToObject(r, "lastHash", s.last_hash);
    cJSON_AddNumberToObject(r, "lastHashDiff", s.last_hash_diff);
    cJSON_AddNumberToObject(r, "hashesChecked", s.hashes_checked);

    const MempoolState &m = s.mempool;
    cJSON *mp = cJSON_AddObjectToObject(r, "mempool");
    cJSON_AddBoolToObject(mp, "valid", m.valid);
    cJSON_AddStringToObject(mp, "error", m.error);
    cJSON_AddNumberToObject(mp, "feeFast", m.fee_fast);
    cJSON_AddNumberToObject(mp, "feeHour", m.fee_hour);
    cJSON_AddNumberToObject(mp, "feeEco", m.fee_eco);
    cJSON *mined = cJSON_AddArrayToObject(mp, "mined");
    for (const MempoolBlock &b : m.mined) {
        if (!b.height) continue;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "height", b.height);
        cJSON_AddNumberToObject(o, "txs", b.txs);
        cJSON_AddNumberToObject(o, "size", b.size_mb);
        cJSON_AddNumberToObject(o, "time", b.time);
        cJSON_AddNumberToObject(o, "fee", b.median_fee);
        cJSON_AddNumberToObject(o, "fees", b.fees_btc);
        cJSON_AddStringToObject(o, "pool", b.pool);
        cJSON_AddItemToArray(mined, o);
    }
    cJSON *nx = cJSON_AddArrayToObject(mp, "next");
    for (const MempoolNext &n : m.next) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "txs", n.txs);
        cJSON_AddNumberToObject(o, "vsize", n.vsize_mb);
        cJSON_AddNumberToObject(o, "fee", n.median_fee);
        cJSON_AddNumberToObject(o, "min", n.min_fee);
        cJSON_AddNumberToObject(o, "max", n.max_fee);
        cJSON_AddItemToArray(nx, o);
    }

    const TunerState &t = s.tuner;
    cJSON *tu = cJSON_AddObjectToObject(r, "tuner");
    cJSON_AddStringToObject(tu, "phase", phase_name(t.phase));
    cJSON_AddStringToObject(tu, "note", t.note);
    cJSON_AddNumberToObject(tu, "progress", t.progress);
    cJSON_AddNumberToObject(tu, "steps", t.steps_done);
    cJSON_AddNumberToObject(tu, "bestFreq", t.best_freq);
    cJSON_AddNumberToObject(tu, "bestMv", t.best_mv);
    cJSON_AddNumberToObject(tu, "bestGhs", t.best_ghs);
    cJSON_AddNumberToObject(tu, "trialFreq", t.trial_freq);
    cJSON_AddNumberToObject(tu, "trialMv", t.trial_mv);

    cJSON *chips = cJSON_AddArrayToObject(r, "chips");
    for (int i = 0; i < AF_NUM_ASICS; i++) {
        cJSON *c = cJSON_CreateObject();
        cJSON_AddNumberToObject(c, "ghs", s.chip_ghs[i]);
        cJSON_AddNumberToObject(c, "errGhs", s.chip_err_ghs[i]);
        cJSON_AddNumberToObject(c, "temp", s.chip_temp[i]);
        cJSON_AddBoolToObject(c, "ownSensor", s.chip_temp_own[i]);
        cJSON_AddItemToArray(chips, c);
    }

    const WeatherState &w = s.weather;
    cJSON *wx = cJSON_AddObjectToObject(r, "weather");
    cJSON_AddBoolToObject(wx, "valid", w.valid);
    cJSON_AddStringToObject(wx, "city", w.city);
    cJSON_AddNumberToObject(wx, "temp", w.temp);
    cJSON_AddNumberToObject(wx, "code", w.code);
    cJSON_AddStringToObject(wx, "text", weather::describe(w.code));
    cJSON_AddStringToObject(wx, "error", w.error);
    return send_json(req, r);
}

static esp_err_t history_get(httpd_req_t *req)
{
    cJSON *a = cJSON_CreateArray();
    for (int i = 0; i < AF_HISTORY_LEN; i++) {
        float h = g_state.history[(g_state.history_head + i) % AF_HISTORY_LEN];
        cJSON_AddItemToArray(a, cJSON_CreateNumber((int) h));
    }
    return send_json(req, a);
}

static esp_err_t settings_get(httpd_req_t *req)
{
    const Settings &c = settings::get();
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "ssid", c.wifi_ssid);
    cJSON_AddStringToObject(r, "pages", c.pages);
    cJSON_AddNumberToObject(r, "disptype", c.display_type);
    cJSON_AddBoolToObject(r, "mempool", c.mempool_on);
    cJSON_AddStringToObject(r, "mpurl", c.mempool_url);
    cJSON_AddBoolToObject(r, "atune", c.autotune);
    cJSON_AddNumberToObject(r, "freq", c.freq_mhz);
    cJSON_AddNumberToObject(r, "mv", c.core_mv);
    cJSON_AddNumberToObject(r, "fantgt", c.fan_target_c);
    cJSON_AddNumberToObject(r, "fanmin", c.fan_min_pct);
    cJSON_AddNumberToObject(r, "bright", c.brightness);
    cJSON_AddNumberToObject(r, "defpage", c.default_page);
    cJSON_AddNumberToObject(r, "rotate", c.page_rotate_s);
    cJSON_AddBoolToObject(r, "rot180", c.rotate_180);
    cJSON_AddStringToObject(r, "city", c.city);
    cJSON_AddBoolToObject(r, "fahr", c.fahrenheit);
    cJSON_AddBoolToObject(r, "h24", c.clock_24h);
    cJSON *lim = cJSON_AddObjectToObject(r, "limits");
    cJSON_AddNumberToObject(lim, "maxFreq", CONFIG_AF_MAX_FREQ_MHZ);
    cJSON_AddNumberToObject(lim, "minMv", CONFIG_AF_MIN_CORE_MV);
    cJSON_AddNumberToObject(lim, "maxMv", CONFIG_AF_MAX_CORE_MV);
    cJSON_AddNumberToObject(lim, "chipCeil", CONFIG_AF_MAX_CHIP_TEMP);
    cJSON_AddNumberToObject(lim, "chipCrit", CONFIG_AF_CRIT_CHIP_TEMP);
    cJSON_AddNumberToObject(lim, "vrCeil", CONFIG_AF_MAX_VR_TEMP);
    cJSON_AddNumberToObject(lim, "vrCrit", CONFIG_AF_CRIT_VR_TEMP);
    cJSON_AddNumberToObject(lim, "maxPower", CONFIG_AF_MAX_POWER_W);
    return send_json(req, r);
}

static bool copy_str(cJSON *root, const char *key, char *dst, size_t len)
{
    cJSON *v = cJSON_GetObjectItem(root, key);
    if (!cJSON_IsString(v)) return false;
    bool changed = strncmp(dst, v->valuestring, len) != 0;
    strncpy(dst, v->valuestring, len - 1);
    dst[len - 1] = 0;
    return changed;
}

static int get_int(cJSON *root, const char *key, int def, int lo, int hi)
{
    cJSON *v = cJSON_GetObjectItem(root, key);
    int r = def;
    if (cJSON_IsNumber(v)) r = v->valueint;
    else if (cJSON_IsString(v) && v->valuestring[0]) r = atoi(v->valuestring);
    return r < lo ? lo : r > hi ? hi : r;
}

static bool get_bool(cJSON *root, const char *key, bool def)
{
    cJSON *v = cJSON_GetObjectItem(root, key);
    return cJSON_IsBool(v) ? cJSON_IsTrue(v) : def;
}

static void reboot_task(void *)
{
    vTaskDelay(pdMS_TO_TICKS(1000));
    board::shutdown();
    esp_restart();
}

static esp_err_t settings_post(httpd_req_t *req)
{
    char body[1536];
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty");
    body[len] = 0;
    cJSON *j = cJSON_Parse(body);
    if (!j) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");

    Settings &c = settings::get();
    bool reboot = false;
    // network / pool: applied after a reboot
    reboot |= copy_str(j, "ssid", c.wifi_ssid, sizeof(c.wifi_ssid));
    cJSON *wp = cJSON_GetObjectItem(j, "wpass");
    if (cJSON_IsString(wp) && wp->valuestring[0]) reboot |= copy_str(j, "wpass", c.wifi_pass, sizeof(c.wifi_pass));

    // tuning: picked up live by the tuner
    c.autotune = get_bool(j, "atune", c.autotune);
    c.freq_mhz = board::clamp_freq(get_int(j, "freq", c.freq_mhz, 0, 2000));
    c.core_mv = board::clamp_mv(get_int(j, "mv", c.core_mv, 0, 2000));
    c.fan_target_c = (uint8_t) get_int(j, "fantgt", c.fan_target_c, 40, 70);
    c.fan_min_pct = (uint8_t) get_int(j, "fanmin", c.fan_min_pct, 10, 100);

    // display: applied immediately
    c.brightness = (uint8_t) get_int(j, "bright", c.brightness, 5, 100);
    c.default_page = (uint8_t) get_int(j, "defpage", c.default_page, 0, 4);
    c.page_rotate_s = (uint8_t) get_int(j, "rotate", c.page_rotate_s, 0, 255);
    bool rot = get_bool(j, "rot180", c.rotate_180);
    if (rot != c.rotate_180) display::set_rotation(rot);
    c.rotate_180 = rot;
    display::set_brightness(c.brightness);

    // clock & weather
    bool wx_changed = copy_str(j, "city", c.city, sizeof(c.city));
    if (wx_changed) c.lat = c.lon = 0;  // re-geocode
    bool fahr = get_bool(j, "fahr", c.fahrenheit);
    wx_changed |= fahr != c.fahrenheit;
    c.fahrenheit = fahr;
    c.clock_24h = get_bool(j, "h24", c.clock_24h);
    if (copy_str(j, "pages", c.pages, sizeof(c.pages))) ui::reload_pages();
    int dt = get_int(j, "disptype", c.display_type, 0, 1);
    if (dt != c.display_type) reboot = true;  // the panel driver is chosen at boot
    c.display_type = (uint8_t) dt;
    bool mp_on = get_bool(j, "mempool", c.mempool_on);
    bool mp_changed = copy_str(j, "mpurl", c.mempool_url, sizeof(c.mempool_url)) || mp_on != c.mempool_on;
    c.mempool_on = mp_on;
    // strip a trailing slash so "/api/..." can be appended
    size_t ml = strlen(c.mempool_url);
    if (ml && c.mempool_url[ml - 1] == '/') c.mempool_url[ml - 1] = 0;
    cJSON_Delete(j);
    settings::save();
    if (wx_changed) weather::refresh_now();
    if (mp_changed) mempool::refresh_now();

    if (reboot) {
        httpd_resp_sendstr(req, "Saved. Rebooting to apply...");
        xTaskCreate(reboot_task, "reboot", 4096, NULL, 5, NULL);  // shutdown + logging needs >2 kB
    } else {
        httpd_resp_sendstr(req, "Saved.");
    }
    return ESP_OK;
}

static esp_err_t retune_post(httpd_req_t *req)
{
    settings::get().autotune = true;
    settings::save();
    tuner::restart();
    return httpd_resp_sendstr(req, "Auto-tune restarted. A full search takes 1-2 hours.");
}

static esp_err_t restart_post(httpd_req_t *req)
{
    httpd_resp_sendstr(req, "Rebooting...");
    xTaskCreate(reboot_task, "reboot", 4096, NULL, 5, NULL);  // shutdown + logging needs >2 kB
    return ESP_OK;
}

// Raw .bin upload (the app image, not the factory image).
static esp_err_t ota_post(httpd_req_t *req)
{
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no OTA partition");
    if (req->content_len < 0x10000 || req->content_len > part->size)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "file size looks wrong");
    ESP_LOGW(TAG, "OTA upload started (%d bytes)", req->content_len);

    esp_ota_handle_t h;
    if (esp_ota_begin(part, OTA_SIZE_UNKNOWN, &h) != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota begin failed");

    // mining keeps running; only the HTTP task is busy while writing
    char *buf = (char *) malloc(4096);
    int remaining = req->content_len;
    bool ok = true, first = true;
    while (remaining > 0) {
        int n = httpd_req_recv(req, buf, remaining < 4096 ? remaining : 4096);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) {
            ok = false;
            break;
        }
        if (first) {
            first = false;
            if ((uint8_t) buf[0] != 0xE9) {  // ESP image magic
                ok = false;
                break;
            }
        }
        if (esp_ota_write(h, buf, n) != ESP_OK) {
            ok = false;
            break;
        }
        remaining -= n;
    }
    free(buf);
    if (!ok || esp_ota_end(h) != ESP_OK || esp_ota_set_boot_partition(part) != ESP_OK) {
        if (ok) ESP_LOGE(TAG, "OTA validation failed");
        else esp_ota_abort(h);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Upload rejected: not a valid AxeForge app image");
    }
    ESP_LOGW(TAG, "OTA written to %s, rebooting", part->label);
    httpd_resp_sendstr(req, "Update installed. Rebooting...");
    xTaskCreate(reboot_task, "reboot", 4096, NULL, 5, NULL);  // shutdown + logging needs >2 kB
    return ESP_OK;
}

// ------------------------------------------------------------------ pools
static esp_err_t pools_get(httpd_req_t *req)
{
    char *pools = settings::pools_to_json();
    cJSON *r = cJSON_CreateObject();
    cJSON_AddItemToObject(r, "pools", cJSON_Parse(pools));
    free(pools);
    cJSON_AddStringToObject(r, "mode", settings::get().pool_mode == PoolMode::Failover ? "failover" : "split");
    return send_json(req, r);
}

static esp_err_t pools_post(httpd_req_t *req)
{
    char *body = recv_body(req, 4096);
    if (!body) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty");
    cJSON *j = cJSON_Parse(body);
    free(body);
    if (!j) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
    cJSON *list = cJSON_GetObjectItem(j, "pools");
    if (cJSON_IsArray(list)) {
        char *s = cJSON_PrintUnformatted(list);
        settings::pools_from_json(s);
        free(s);
    }
    cJSON *mode = cJSON_GetObjectItem(j, "mode");
    if (cJSON_IsString(mode)) settings::get().pool_mode = strcmp(mode->valuestring, "failover") ? PoolMode::Split : PoolMode::Failover;
    cJSON_Delete(j);
    settings::save();
    pools::reconfigure();  // reconnects in place, no reboot
    return httpd_resp_sendstr(req, "Pools saved. Reconnecting...");
}

// ------------------------------------------------------------------ logs
static esp_err_t logs_get(httpd_req_t *req)
{
    char q[48], v[16];
    uint32_t since = 0;
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK && httpd_query_key_value(q, "since", v, sizeof(v)) == ESP_OK)
        since = strtoul(v, NULL, 10);
    const size_t max = 12 * 1024;
    char *text = (char *) malloc(max);
    uint32_t next = logbuf::read(since, text, max);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "next", next);
    cJSON_AddStringToObject(r, "text", text);
    free(text);
    return send_json(req, r);
}

// ------------------------------------------------------------------ screen
// Live capture of the real LCD as a 16-bit BMP.
static esp_err_t screen_get(httpd_req_t *req)
{
    lv_img_dsc_t *img = display::snapshot(1500);
    if (!img) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "display busy");
    const int w = img->header.w, h = img->header.h;
    const uint32_t row = w * 2, data = row * h, hdr = 14 + 40 + 12;
    uint8_t head[66] = {'B', 'M'};
    auto put32 = [&](int o, uint32_t v) { memcpy(head + o, &v, 4); };
    auto put16 = [&](int o, uint16_t v) { memcpy(head + o, &v, 2); };
    put32(2, hdr + data);
    put32(10, hdr);
    put32(14, 40);
    put32(18, w);
    put32(22, (uint32_t) -h);  // top-down rows
    put16(26, 1);
    put16(28, 16);
    put32(30, 3);              // BI_BITFIELDS
    put32(34, data);
    put32(54, 0xF800);
    put32(58, 0x07E0);
    put32(62, 0x001F);
    httpd_resp_set_type(req, "image/bmp");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send_chunk(req, (const char *) head, sizeof(head));
    uint8_t *line = (uint8_t *) malloc(row * 8);
    const uint8_t *px = img->data;
    for (int y = 0; y < h; y += 8) {
        int rows = h - y < 8 ? h - y : 8;
        for (int i = 0; i < (int) row * rows; i += 2) {  // LVGL stores swapped bytes for the SPI/i80 panel
            line[i] = px[y * row + i + 1];
            line[i + 1] = px[y * row + i];
        }
        if (httpd_resp_send_chunk(req, (const char *) line, row * rows) != ESP_OK) break;
    }
    free(line);
    display::free_snapshot(img);
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t page_post(httpd_req_t *req)
{
    char *body = recv_body(req, 128);
    cJSON *j = body ? cJSON_Parse(body) : nullptr;
    free(body);
    cJSON *dir = cJSON_GetObjectItem(j, "dir"), *kind = cJSON_GetObjectItem(j, "kind");
    if (cJSON_IsNumber(dir)) ui::request_page(dir->valueint > 0 ? 1 : -1);
    else if (cJSON_IsString(kind) && kind->valuestring[0]) ui::request_kind(kind->valuestring[0]);
    cJSON_Delete(j);
    vTaskDelay(pdMS_TO_TICKS(600));  // let the display switch before the next screenshot
    char out[32];
    snprintf(out, sizeof(out), "{\"kind\":\"%c\"}", ui::current_kind());
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, out);
}

// ------------------------------------------------------------------ layout
static esp_err_t layout_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, layout_store::json());
}

static esp_err_t layout_post(httpd_req_t *req)
{
    char *body = recv_body(req, 6144);
    if (!body) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty");
    bool ok = layout_store::set_json(body);
    free(body);
    if (!ok) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid layout");
    return httpd_resp_sendstr(req, "Layout saved and applied.");
}

namespace web {

void start()
{
    httpd_handle_t h = NULL;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    cfg.max_uri_handlers = 24;
    cfg.recv_wait_timeout = 15;
    cfg.send_wait_timeout = 10;
    cfg.max_open_sockets = 8;
    cfg.lru_purge_enable = true;  // a browser holding idle keep-alives must not lock others out
    if (httpd_start(&h, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "http server failed to start");
        return;
    }
    const httpd_uri_t uris[] = {
        {"/", HTTP_GET, index_get, NULL},
        {"/api/status", HTTP_GET, status_get, NULL},
        {"/favicon.ico", HTTP_GET, favicon_get, NULL},
        {"/api/history", HTTP_GET, history_get, NULL},
        {"/api/settings", HTTP_GET, settings_get, NULL},
        {"/api/settings", HTTP_POST, settings_post, NULL},
        {"/api/retune", HTTP_POST, retune_post, NULL},
        {"/api/restart", HTTP_POST, restart_post, NULL},
        {"/api/ota", HTTP_POST, ota_post, NULL},
        {"/api/pools", HTTP_GET, pools_get, NULL},
        {"/api/pools", HTTP_POST, pools_post, NULL},
        {"/api/logs", HTTP_GET, logs_get, NULL},
        {"/api/screen.bmp", HTTP_GET, screen_get, NULL},
        {"/api/page", HTTP_POST, page_post, NULL},
        {"/api/layout", HTTP_GET, layout_get, NULL},
        {"/api/layout", HTTP_POST, layout_post, NULL},
    };
    for (const auto &u : uris) httpd_register_uri_handler(h, &u);
}

} // namespace web
