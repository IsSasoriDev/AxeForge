#include "mempool.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

#include "core/settings.h"
#include "core/state.h"
#include "net/http_util.h"
#include "net/wifi.h"
#include "core/json_util.h"  // depth-limited parse for untrusted JSON

static const char *TAG = "mempool";

#define POLL_S 60
#define BLOCKS_MAX (48 * 1024)

static TaskHandle_t s_task;

static double num(cJSON *o, const char *k)
{
    cJSON *v = cJSON_GetObjectItem(o, k);
    return cJSON_IsNumber(v) ? v->valuedouble : 0;
}

static bool fetch_blocks(const char *base, MempoolState &m)
{
    char url[128];
    snprintf(url, sizeof(url), "%s/api/v1/blocks", base);
    char *body = http::get(url, BLOCKS_MAX);
    if (!body) return false;
    cJSON *a = json_parse(body);
    free(body);
    if (!cJSON_IsArray(a)) {
        cJSON_Delete(a);
        return false;
    }
    for (int i = 0; i < 6; i++) {
        cJSON *b = cJSON_GetArrayItem(a, i);
        MempoolBlock &mb = m.mined[i];
        mb = MempoolBlock();
        if (!b) continue;
        mb.height = (uint32_t) num(b, "height");
        mb.txs = (uint32_t) num(b, "tx_count");
        mb.size_mb = (float) (num(b, "size") / 1e6);
        mb.time = (uint32_t) num(b, "timestamp");
        cJSON *ex = cJSON_GetObjectItem(b, "extras");
        if (ex) {
            mb.median_fee = (float) num(ex, "medianFee");
            mb.fees_btc = (float) (num(ex, "totalFees") / 1e8);
            cJSON *pool = cJSON_GetObjectItem(ex, "pool");
            cJSON *name = pool ? cJSON_GetObjectItem(pool, "name") : NULL;
            if (cJSON_IsString(name)) strncpy(mb.pool, name->valuestring, sizeof(mb.pool) - 1);
        }
    }
    cJSON_Delete(a);
    return true;
}

static bool fetch_projected(const char *base, MempoolState &m)
{
    char url[128];
    snprintf(url, sizeof(url), "%s/api/v1/fees/mempool-blocks", base);
    char *body = http::get(url, 16 * 1024);
    if (!body) return false;
    cJSON *a = json_parse(body);
    free(body);
    if (!cJSON_IsArray(a)) {
        cJSON_Delete(a);
        return false;
    }
    for (int i = 0; i < 3; i++) {
        cJSON *b = cJSON_GetArrayItem(a, i);
        MempoolNext &n = m.next[i];
        n = MempoolNext();
        if (!b) continue;
        n.txs = (uint32_t) num(b, "nTx");
        n.vsize_mb = (float) (num(b, "blockVSize") / 1e6);
        n.median_fee = (float) num(b, "medianFee");
        cJSON *r = cJSON_GetObjectItem(b, "feeRange");
        int rn = cJSON_GetArraySize(r);
        if (rn > 0) {
            n.min_fee = (float) cJSON_GetArrayItem(r, 0)->valuedouble;
            n.max_fee = (float) cJSON_GetArrayItem(r, rn - 1)->valuedouble;
        }
    }
    cJSON_Delete(a);

    snprintf(url, sizeof(url), "%s/api/v1/fees/recommended", base);
    body = http::get(url, 1024);
    if (body) {
        cJSON *f = json_parse(body);
        free(body);
        m.fee_fast = (float) num(f, "fastestFee");
        m.fee_hour = (float) num(f, "hourFee");
        m.fee_eco = (float) num(f, "economyFee");
        cJSON_Delete(f);
    }
    return true;
}

static void mempool_task(void *)
{
    // Work on a copy so the UI never sees a half-updated set.
    static MempoolState next;
    while (true) {
        const Settings &cfg = settings::get();
        uint32_t wait = POLL_S;
        if (!cfg.mempool_on) {
            g_state.mempool.valid = false;
            snprintf(g_state.mempool.error, sizeof(g_state.mempool.error), "Mempool feed disabled");
            wait = 3600;
        } else if (wifi::connected()) {
            next = g_state.mempool;
            bool ok = fetch_blocks(cfg.mempool_url, next);
            ok = fetch_projected(cfg.mempool_url, next) && ok;
            if (ok) {
                next.valid = true;
                next.updated_s = g_state.uptime_s;
                next.error[0] = 0;
                g_state.mempool = next;
                ESP_LOGI(TAG, "tip %lu, next block ~%.1f sat/vB", (unsigned long) next.mined[0].height, next.next[0].median_fee);
            } else {
                snprintf(g_state.mempool.error, sizeof(g_state.mempool.error), "Can't reach %.30s", cfg.mempool_url);
                wait = 20;
            }
        } else {
            wait = 5;
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait * 1000));
    }
}

namespace mempool {
void start()
{
    // PSRAM stack: this task only does HTTP, never flash writes
    xTaskCreatePinnedToCoreWithCaps(mempool_task, "mempool", 8192, NULL, 3, &s_task, tskNO_AFFINITY, MALLOC_CAP_SPIRAM);
}
void refresh_now()
{
    if (s_task) xTaskNotifyGive(s_task);
}
}
