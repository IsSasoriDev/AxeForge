#include "otp.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "mbedtls/md.h"
#include "nvs.h"

#include "core/state.h"

static const char *TAG = "otp";

#define SECRET_LEN 20          // 160 bits, the RFC 4226 recommendation
#define SESSIONS 4
#define SESSION_S (12 * 3600)  // a login lasts 12 hours (or until a reboot)
#define MAX_FAILS 5            // wrong codes before a one-minute lockout

static uint8_t s_secret[SECRET_LEN], s_pending[SECRET_LEN];
static bool s_enabled = false, s_have_pending = false;
static uint64_t s_last_step = 0;  // a code is accepted once (RFC 6238 5.2)
static int s_fails = 0;
static uint32_t s_locked_until = 0;
struct Session {
    char token[33];
    uint32_t expires;  // uptime seconds
};
static Session s_sessions[SESSIONS];
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static void base32(const uint8_t *in, size_t n, char *out, size_t olen)
{
    static const char A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    size_t o = 0;
    uint32_t buf = 0;
    int bits = 0;
    for (size_t i = 0; i < n && o + 1 < olen; i++) {
        buf = (buf << 8) | in[i];
        bits += 8;
        while (bits >= 5 && o + 1 < olen) {
            out[o++] = A[(buf >> (bits - 5)) & 31];
            bits -= 5;
        }
    }
    if (bits > 0 && o + 1 < olen) out[o++] = A[(buf << (5 - bits)) & 31];
    out[o] = 0;
}

static uint32_t hotp(const uint8_t *key, uint64_t counter)
{
    uint8_t msg[8], mac[20];
    for (int i = 7; i >= 0; i--) {
        msg[i] = counter & 0xff;
        counter >>= 8;
    }
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA1), key, SECRET_LEN, msg, 8, mac);
    int off = mac[19] & 0x0f;
    uint32_t bin = ((mac[off] & 0x7f) << 24) | (mac[off + 1] << 16) | (mac[off + 2] << 8) | mac[off + 3];
    return bin % 1000000;
}

// Checks `code` against the steps around now. Returns the matching step, or 0.
static const char *check(const uint8_t *key, const char *code, uint64_t *step_out)
{
    if (s_locked_until && g_state.uptime_s < s_locked_until) return "Too many wrong codes. Wait a minute.";
    if (!g_state.time_synced) return "The miner's clock isn't set yet (no internet time). Try again shortly.";
    if (strlen(code) != 6) return "Enter the 6-digit code.";
    uint32_t want = 0;
    for (int i = 0; i < 6; i++) {
        if (code[i] < '0' || code[i] > '9') return "Enter the 6-digit code.";
        want = want * 10 + (code[i] - '0');
    }
    uint64_t now = (uint64_t) time(NULL) / 30;
    for (int d = -1; d <= 1; d++) {  // one step of clock drift either way
        uint64_t step = now + d;
        if (step <= s_last_step) continue;
        if (hotp(key, step) == want) {
            s_fails = 0;
            *step_out = step;
            return nullptr;
        }
    }
    if (++s_fails >= MAX_FAILS) {
        s_fails = 0;
        s_locked_until = g_state.uptime_s + 60;
        ESP_LOGW(TAG, "%d wrong codes, locked for a minute", MAX_FAILS);
    }
    return "Wrong code.";
}

static void new_session(char *token, size_t tlen)
{
    uint8_t r[16];
    esp_fill_random(r, sizeof(r));
    char t[33];
    for (int i = 0; i < 16; i++) snprintf(t + 2 * i, 3, "%02x", r[i]);
    portENTER_CRITICAL(&s_mux);
    int slot = 0;
    for (int i = 1; i < SESSIONS; i++)  // reuse the oldest
        if (s_sessions[i].expires < s_sessions[slot].expires) slot = i;
    memcpy(s_sessions[slot].token, t, sizeof(t));
    s_sessions[slot].expires = g_state.uptime_s + SESSION_S;
    portEXIT_CRITICAL(&s_mux);
    snprintf(token, tlen, "%s", t);
}

static void save()
{
    nvs_handle_t h;
    if (nvs_open("axeforge", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "otp_on", s_enabled);
    if (s_enabled) nvs_set_blob(h, "otp_key", s_secret, SECRET_LEN);
    else nvs_erase_key(h, "otp_key");
    nvs_commit(h);
    nvs_close(h);
}

namespace otp {

void load()
{
    nvs_handle_t h;
    if (nvs_open("axeforge", NVS_READONLY, &h) != ESP_OK) return;
    uint8_t on = 0;
    size_t len = SECRET_LEN;
    if (nvs_get_u8(h, "otp_on", &on) == ESP_OK && on && nvs_get_blob(h, "otp_key", s_secret, &len) == ESP_OK &&
        len == SECRET_LEN)
        s_enabled = true;
    nvs_close(h);
    if (s_enabled) ESP_LOGI(TAG, "two-factor login is on");
}

bool enabled() { return s_enabled; }

void begin_setup(char *secret_b32, size_t slen, char *uri, size_t ulen)
{
    esp_fill_random(s_pending, SECRET_LEN);
    s_have_pending = true;
    base32(s_pending, SECRET_LEN, secret_b32, slen);
    snprintf(uri, ulen, "otpauth://totp/AxeForge:%s?secret=%s&issuer=AxeForge&digits=6&period=30",
             g_state.hostname[0] ? g_state.hostname : "miner", secret_b32);
}

const char *confirm(const char *code, char *token, size_t tlen)
{
    if (!s_have_pending) return "Start the setup first.";
    uint64_t step;
    const char *err = check(s_pending, code, &step);
    if (err) return err;
    memcpy(s_secret, s_pending, SECRET_LEN);
    memset(s_pending, 0, SECRET_LEN);
    s_have_pending = false;
    s_enabled = true;
    s_last_step = step;
    save();
    ESP_LOGW(TAG, "two-factor login enabled");
    new_session(token, tlen);
    return nullptr;
}

const char *login(const char *code, char *token, size_t tlen)
{
    if (!s_enabled) return "Two-factor login is off.";
    uint64_t step;
    const char *err = check(s_secret, code, &step);
    if (err) return err;
    s_last_step = step;
    new_session(token, tlen);
    return nullptr;
}

const char *disable(const char *code)
{
    if (!s_enabled) return nullptr;
    uint64_t step;
    const char *err = check(s_secret, code, &step);
    if (err) return err;
    s_last_step = step;
    s_enabled = false;
    memset(s_secret, 0, SECRET_LEN);
    portENTER_CRITICAL(&s_mux);
    memset(s_sessions, 0, sizeof(s_sessions));
    portEXIT_CRITICAL(&s_mux);
    save();
    ESP_LOGW(TAG, "two-factor login disabled");
    return nullptr;
}

void logout(const char *token)
{
    portENTER_CRITICAL(&s_mux);
    for (Session &s : s_sessions)
        if (s.token[0] && strcmp(s.token, token) == 0) memset(&s, 0, sizeof(s));
    portEXIT_CRITICAL(&s_mux);
}

bool session_ok(const char *token)
{
    if (!token || strlen(token) != 32) return false;
    bool ok = false;
    portENTER_CRITICAL(&s_mux);
    for (const Session &s : s_sessions) {
        // compare every byte, so the time taken doesn't reveal how much matched
        uint8_t diff = 0;
        for (int i = 0; i < 32; i++) diff |= (uint8_t) (s.token[i] ^ token[i]);
        if (s.token[0] && diff == 0 && g_state.uptime_s < s.expires) ok = true;
    }
    portEXIT_CRITICAL(&s_mux);
    return ok;
}

}
