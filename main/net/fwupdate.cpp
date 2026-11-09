#include "fwupdate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"

#include "core/board.h"
#include "core/board_config.h"
#include "core/ota_guard.h"
#include "core/settings.h"
#include "core/state.h"
#include "net/wifi.h"

static const char *TAG = "fwupdate";

#define REPO "https://github.com/IsSasoriDev/AxeForge"
#define CHECK_EVERY_S (6 * 3600)
#define FIRST_CHECK_S 60    // after boot, once Wi-Fi and mining have settled
#define CHUNK 4096

// Looks for "AXEFORGE-BOARD:<id>;" in `data`. Returns the id length (copied to
// `id`) or 0. The bare prefix used by this search sits in rodata too, followed
// by a NUL, so only a complete tag counts.
static int find_board_tag(const char *data, int len, char *id, int id_cap)
{
    const int plen = sizeof(AF_BOARD_TAG_PREFIX) - 1;
    for (int i = 0; i + plen < len; i++) {
        if (data[i] != 'A' || memcmp(data + i, AF_BOARD_TAG_PREFIX, plen) != 0) continue;
        int k = 0;
        const char *p = data + i + plen;
        while (i + plen + k < len && k < id_cap - 1 &&
               ((p[k] >= 'a' && p[k] <= 'z') || (p[k] >= '0' && p[k] <= '9')))
            k++;
        if (k >= 2 && i + plen + k < len && p[k] == ';') {
            memcpy(id, p, k);
            id[k] = 0;
            return k;
        }
    }
    return 0;
}

namespace fwupdate {

// ------------------------------------------------------------------ Writer

Writer::~Writer()
{
    if (m_open) abort();
    free(m_scan);
}

bool Writer::begin()
{
    m_part = esp_ota_get_next_update_partition(NULL);
    if (!m_part) {
        m_err = "no OTA partition";
        return false;
    }
    m_scan = (char *) malloc(CHUNK + KEEP);
    // erase sector by sector as data arrives: a one-shot 4 MB erase stalls the CPU
    // for seconds, long enough for the sensor reads to fail and trip the cut-off
    if (!m_scan || esp_ota_begin(m_part, OTA_WITH_SEQUENTIAL_WRITES, &m_h) != ESP_OK) {
        m_err = "ota begin failed";
        return false;
    }
    m_open = true;
    return true;
}

bool Writer::feed(const char *data, int n)
{
    while (n > 0) {
        int take = n < CHUNK ? n : CHUNK;
        if (m_first) {
            m_first = false;
            if ((uint8_t) data[0] != 0xE9) {  // ESP image magic
                m_err = "not a valid AxeForge app image";
                return false;
            }
        }
        // scan the end of the previous piece together with this one
        memcpy(m_scan + m_carry, data, take);
        int total = m_carry + take;
        if (!m_other[0] && find_board_tag(m_scan, total, m_other, sizeof(m_other)) &&
            strcmp(m_other, AF_BOARD_ID) != 0) {
            m_wrong = true;
            m_err = "the image is for another board";
            return false;
        }
        if (esp_ota_write(m_h, data, take) != ESP_OK) {
            m_err = "flash write failed";
            return false;
        }
        m_carry = total < KEEP ? total : KEEP;
        memmove(m_scan, m_scan + total - m_carry, m_carry);
        data += take;
        n -= take;
    }
    return true;
}

bool Writer::finish()
{
    if (!m_other[0]) {
        // every AxeForge image carries its board tag; one without it isn't ours
        m_err = "that file is not an AxeForge firmware for this miner";
        abort();
        return false;
    }
    m_open = false;
    if (esp_ota_end(m_h) != ESP_OK) {
        m_err = "not a valid AxeForge app image";
        return false;
    }
    if (esp_ota_set_boot_partition(m_part) != ESP_OK) {
        m_err = "could not select the new firmware";
        return false;
    }
    return true;
}

void Writer::abort()
{
    if (m_open) esp_ota_abort(m_h);
    m_open = false;
}

// ------------------------------------------------------------------ self-update

static Info s_info = {"", false, State::Idle, 0, ""};
static volatile bool s_check_req = false, s_install_req = false;
static char s_failed_tag[24] = "";  // auto-update doesn't retry a release that failed until the next check

const Info &info() { return s_info; }

const char *state_name(State s)
{
    switch (s) {
    case State::Checking: return "checking";
    case State::Downloading: return "downloading";
    case State::Failed: return "failed";
    default: return "idle";
    }
}

static bool parse_ver(const char *v, int out[3])
{
    if (*v == 'v') v++;
    return sscanf(v, "%d.%d.%d", &out[0], &out[1], &out[2]) == 3;
}

static bool is_newer(const char *tag, const char *cur)
{
    int a[3], b[3];
    if (!parse_ver(tag, a) || !parse_ver(cur, b)) return false;
    for (int i = 0; i < 3; i++)
        if (a[i] != b[i]) return a[i] > b[i];
    return false;
}

// The release file for this board, as the release workflow names it
static void asset_name(char *out, size_t n)
{
    if (strcmp(AF_BOARD_ID, "nerdqaxe") == 0) snprintf(out, n, "axeforge.bin");
    else snprintf(out, n, "axeforge-%s.bin", AF_BOARD_ID);
}

// the response's Location header (esp_http_client_get_header only reads the request's)
static esp_err_t on_event(esp_http_client_event_t *e)
{
    if (e->event_id == HTTP_EVENT_ON_HEADER && e->user_data && strcasecmp(e->header_key, "Location") == 0)
        snprintf((char *) e->user_data, 160, "%s", e->header_value);
    return ESP_OK;
}

// github.com/<repo>/releases/latest answers with a redirect to .../tag/<tag>: the
// Location header is all that is needed, no API call, no JSON.
static void do_check()
{
    s_info.state = State::Checking;
    esp_http_client_config_t cfg = {};
    cfg.url = REPO "/releases/latest";
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms = 10000;
    cfg.disable_auto_redirect = true;
    cfg.method = HTTP_METHOD_HEAD;
    cfg.buffer_size = 1024;
    cfg.buffer_size_tx = 1024;
    char loc[160] = "";
    cfg.event_handler = on_event;
    cfg.user_data = loc;
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    char tag[24] = "";
    if (c && esp_http_client_perform(c) == ESP_OK) {
        int st = esp_http_client_get_status_code(c);
        if ((st == 301 || st == 302) && loc[0]) {
            const char *t = strstr(loc, "/tag/");
            if (t) {
                snprintf(tag, sizeof(tag), "%s", t + 5);
                tag[strcspn(tag, "/?#\r\n ")] = 0;
            }
        }
        if (!tag[0]) ESP_LOGW(TAG, "release check: unexpected answer (HTTP %d)", st);
    } else {
        ESP_LOGW(TAG, "release check: GitHub not reachable");
    }
    if (c) esp_http_client_cleanup(c);
    if (!tag[0]) {
        snprintf(s_info.msg, sizeof(s_info.msg), "Couldn't reach GitHub to check for updates");
        s_info.state = State::Idle;
        return;
    }
    const char *cur = esp_app_get_description()->version;
    snprintf(s_info.latest, sizeof(s_info.latest), "%s", tag);
    s_info.newer = is_newer(tag, cur);
    if (s_info.newer) snprintf(s_info.msg, sizeof(s_info.msg), "AxeForge %s is available", tag);
    else snprintf(s_info.msg, sizeof(s_info.msg), "Up to date (latest release %s)", tag);
    ESP_LOGI(TAG, "latest release %s, running %s%s", tag, cur, s_info.newer ? ": update available" : "");
    s_info.state = State::Idle;
}

static void fail(const char *why)
{
    ESP_LOGE(TAG, "update failed: %s", why);
    snprintf(s_info.msg, sizeof(s_info.msg), "Update failed: %s", why);
    snprintf(s_failed_tag, sizeof(s_failed_tag), "%s", s_info.latest);
    s_info.state = State::Failed;
}

static void do_install()
{
    char asset[48], url[160];
    asset_name(asset, sizeof(asset));
    snprintf(url, sizeof(url), REPO "/releases/download/%s/%s", s_info.latest, asset);
    ESP_LOGW(TAG, "installing %s from %s", s_info.latest, url);
    s_info.state = State::Downloading;
    s_info.pct = 0;
    snprintf(s_info.msg, sizeof(s_info.msg), "Downloading %s", s_info.latest);

    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms = 15000;
    cfg.buffer_size = 4096;     // GitHub's asset redirect carries long headers
    cfg.buffer_size_tx = 2048;  // and an even longer signed URL to follow
    cfg.keep_alive_enable = true;
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return fail("out of memory");

    // the asset URL redirects to GitHub's file storage
    int64_t len = -1;
    int st = 0;
    for (int hops = 0;; hops++) {
        if (esp_http_client_open(c, 0) != ESP_OK) {
            esp_http_client_cleanup(c);
            return fail("can't reach GitHub");
        }
        len = esp_http_client_fetch_headers(c);
        st = esp_http_client_get_status_code(c);
        if ((st == 301 || st == 302 || st == 303 || st == 307 || st == 308) && hops < 5) {
            esp_http_client_flush_response(c, NULL);
            esp_http_client_set_redirection(c);
            esp_http_client_close(c);
            continue;
        }
        break;
    }
    if (st != 200) {
        esp_http_client_cleanup(c);
        char why[48];
        snprintf(why, sizeof(why), "%s answered HTTP %d", st == 404 ? "no file for this board:" : "GitHub", st);
        return fail(why);
    }

    Writer w;
    if (!w.begin()) {
        esp_http_client_cleanup(c);
        return fail(w.error());
    }
    if (len > 0 && (len < 0x10000 || len > (int64_t) w.partition()->size)) {
        esp_http_client_cleanup(c);
        return fail("file size looks wrong");
    }
    char *buf = (char *) malloc(CHUNK);
    if (!buf) {
        esp_http_client_cleanup(c);
        return fail("out of memory");
    }
    int64_t got = 0;
    bool ok = true;
    while (true) {
        int n = esp_http_client_read(c, buf, CHUNK);
        if (n < 0) {
            ok = false;
            fail("download interrupted");
            break;
        }
        if (n == 0) {
            if (len > 0 && got < len) {
                ok = false;
                fail("download ended early");
            }
            break;
        }
        if (!w.feed(buf, n)) {
            ok = false;
            fail(w.error());
            break;
        }
        got += n;
        if (len > 0) s_info.pct = (uint8_t) (got * 100 / len);
    }
    free(buf);
    esp_http_client_cleanup(c);
    if (!ok) {
        w.abort();
        return;
    }
    if (!w.finish()) return fail(w.error());

    ota_guard::armed(w.partition());
    s_info.pct = 100;
    snprintf(s_info.msg, sizeof(s_info.msg), "Installed %s, restarting", s_info.latest);
    ESP_LOGW(TAG, "%s written to %s, restarting", s_info.latest, w.partition()->label);
    vTaskDelay(pdMS_TO_TICKS(1500));  // let the web UI see it
    board::shutdown();
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

// Auto-update waits for a healthy miner: not right after an update (that one is
// still on probation), not in a fault, and not twice for a release that failed.
static bool auto_ok()
{
    if (!settings::get().auto_update || !s_info.newer) return false;
    if (ota_guard::pending()) return false;
    if (g_state.status == SysStatus::Fault || g_state.status == SysStatus::Overheat) return false;
    return strcmp(s_failed_tag, s_info.latest) != 0;
}

// Flash writes need a task whose stack is in internal RAM, so the install runs in
// its own task, made only for it; the periodic check lives on a PSRAM stack.
static volatile bool s_installing = false;

static void install_task(void *)
{
    do_install();  // restarts on success
    s_installing = false;
    vTaskDelete(NULL);
}

static void spawn_install()
{
    if (s_installing) return;
    s_installing = true;
    s_info.state = State::Downloading;  // the web UI shows it at once
    s_info.pct = 0;
    if (xTaskCreate(install_task, "fwinstall", 8192, NULL, 3, NULL) != pdPASS) {
        s_installing = false;
        fail("not enough memory to start the download");
    }
}

static void task(void *)
{
    int64_t next_s = FIRST_CHECK_S;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        int64_t now_s = esp_timer_get_time() / 1000000;
        if (!wifi::connected()) continue;
        if (s_check_req || now_s >= next_s) {
            bool asked = s_check_req;
            s_check_req = false;
            next_s = now_s + CHECK_EVERY_S;
            if (asked) s_failed_tag[0] = 0;  // a manual check clears the failure
            do_check();
            if (auto_ok()) {
                ESP_LOGW(TAG, "auto-update on: installing %s", s_info.latest);
                spawn_install();
            }
        }
        if (s_install_req) {
            s_install_req = false;
            if (!s_info.newer) do_check();
            if (s_info.newer) spawn_install();
        }
    }
}

void start()
{
    // the check only talks HTTPS: its stack can be in PSRAM, as the other web clients'
    xTaskCreateWithCaps(task, "fwupdate", 6144, NULL, 2, NULL, MALLOC_CAP_SPIRAM);
}

void check_now() { s_check_req = true; }

bool install_now()
{
    if (s_installing || s_info.state == State::Checking) return false;
    if (!s_info.newer) return false;
    s_install_req = true;
    return true;
}

} // namespace fwupdate
