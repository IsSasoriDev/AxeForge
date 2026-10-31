#include "energy.h"

#include <math.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "nvs.h"

#include "core/board.h"
#include "core/settings.h"
#include "core/state.h"
#include "mining/work.h"
#include "net/price.h"
#include "net/weather.h"

static const char *TAG = "energy";

#define NS "af_energy"
#define SAVE_EVERY_S 3600

static struct Totals {
    uint32_t magic;
    double kwh_total, cost_total;
    double kwh_today, cost_today;
    int32_t day;      // local day of the "today" figures (tm_yday + 1000 * year)
    uint32_t since;   // unix time the totals started
} s_tot;
#define MAGIC 0x454E5247

static void save()
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "tot", &s_tot, sizeof(s_tot));
    nvs_commit(h);
    nvs_close(h);
}

// The price per kWh in the user's money: their own setting, or else the market
// price from the price mode (ct/kWh, Energy-Charts). NAN when neither is known.
static double price_per_kwh(const char **source)
{
    const Settings &c = settings::get();
    if (c.energy_price > 0) {
        *source = "yours";
        return c.energy_price;
    }
    float ct = price::market_ct_kwh();
    if (!isnan(ct)) {
        *source = "market";
        return ct / 100.0;
    }
    *source = "none";
    return NAN;
}

namespace energy {

void load()
{
    nvs_handle_t h;
    size_t n = sizeof(s_tot);
    bool ok = false;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        ok = nvs_get_blob(h, "tot", &s_tot, &n) == ESP_OK && n == sizeof(s_tot) && s_tot.magic == MAGIC;
        nvs_close(h);
    }
    if (!ok) {
        memset(&s_tot, 0, sizeof(s_tot));
        s_tot.magic = MAGIC;
    }
}

void tick()
{
    static uint32_t since_save = 0;
    struct tm tm;
    if (weather::local_time(&tm)) {
        int32_t day = tm.tm_yday + 1000 * tm.tm_year;
        if (day != s_tot.day) {  // a new day (or the first one since the clock synced)
            s_tot.day = day;
            s_tot.kwh_today = s_tot.cost_today = 0;
        }
        if (!s_tot.since) s_tot.since = (uint32_t) time(NULL);
    }
    if (board::is_powered() && g_state.pin > 0) {
        double kwh = g_state.pin / 3600.0 / 1000.0;
        const char *src;
        double p = price_per_kwh(&src);
        s_tot.kwh_total += kwh;
        s_tot.kwh_today += kwh;
        if (!isnan(p)) {
            s_tot.cost_total += kwh * p;
            s_tot.cost_today += kwh * p;
        }
    }
    if (++since_save >= SAVE_EVERY_S) {
        since_save = 0;
        save();
    }
}

void reset_totals()
{
    memset(&s_tot, 0, sizeof(s_tot));
    s_tot.magic = MAGIC;
    s_tot.since = (uint32_t) time(NULL);
    save();
    ESP_LOGI(TAG, "energy totals reset");
}

cJSON *json()
{
    const Settings &c = settings::get();
    cJSON *r = cJSON_CreateObject();
    const char *src;
    double p = price_per_kwh(&src);
    cJSON_AddStringToObject(r, "currency", c.currency);
    cJSON_AddStringToObject(r, "priceFrom", src);
    if (!isnan(p)) cJSON_AddNumberToObject(r, "price", round(p * 10000) / 10000);
    cJSON_AddNumberToObject(r, "kwhToday", round(s_tot.kwh_today * 1000) / 1000);
    cJSON_AddNumberToObject(r, "kwhTotal", round(s_tot.kwh_total * 1000) / 1000);
    cJSON_AddNumberToObject(r, "costToday", round(s_tot.cost_today * 100) / 100);
    cJSON_AddNumberToObject(r, "costTotal", round(s_tot.cost_total * 100) / 100);
    cJSON_AddNumberToObject(r, "since", s_tot.since);
    double w = board::is_powered() ? g_state.pin : 0;
    if (!isnan(p)) cJSON_AddNumberToObject(r, "costPerDay", round(w * 24 / 1000 * p * 100) / 100);

    // block odds at the current hashrate against the network difficulty
    double hps = g_state.hashrate_ghs * 1e9;
    double diff = g_state.network_nbits ? work::nbits_to_difficulty(g_state.network_nbits) : 0;
    if (hps > 0 && diff > 0) {
        double mean_s = diff * 4294967296.0 / hps;  // expected time to a block
        auto odds = [&](double s) { return -expm1(-s / mean_s); };
        cJSON_AddNumberToObject(r, "oddsDay", odds(86400));
        cJSON_AddNumberToObject(r, "oddsMonth", odds(86400 * 30.4));
        cJSON_AddNumberToObject(r, "oddsYear", odds(86400 * 365.25));
        cJSON_AddNumberToObject(r, "yearsPerBlock", mean_s / (86400 * 365.25));
        if (!isnan(p) && w > 0) cJSON_AddNumberToObject(r, "costPerBlock", round(w / 1000 * mean_s / 3600 * p));
    }
    return r;
}

}
