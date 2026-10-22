#include "layout_store.h"

#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

static const char *TAG = "layout";

#define MAX_JSON 4096

static const char *DEFAULT_LAYOUT =
    "{\"pages\":["
    "[{\"t\":\"gauge\",\"x\":0,\"y\":0,\"w\":5,\"h\":5},{\"t\":\"chart\",\"x\":5,\"y\":0,\"w\":7,\"h\":3},"
    "{\"t\":\"chiptemp\",\"x\":5,\"y\":3,\"w\":3,\"h\":2},{\"t\":\"power\",\"x\":8,\"y\":3,\"w\":4,\"h\":2},"
    "{\"t\":\"lasthash\",\"x\":0,\"y\":5,\"w\":6,\"h\":3},{\"t\":\"blocks\",\"x\":6,\"y\":5,\"w\":6,\"h\":3}],"
    "[{\"t\":\"clock\",\"x\":0,\"y\":0,\"w\":12,\"h\":4},{\"t\":\"weather\",\"x\":0,\"y\":4,\"w\":6,\"h\":4},"
    "{\"t\":\"hashrate\",\"x\":6,\"y\":4,\"w\":6,\"h\":2},{\"t\":\"shares\",\"x\":6,\"y\":6,\"w\":3,\"h\":2},"
    "{\"t\":\"bestdiff\",\"x\":9,\"y\":6,\"w\":3,\"h\":2}]"
    "]}";

static char *s_json;
static volatile bool s_dirty = false;

// Clamp every widget into the grid; reject anything that isn't the expected shape.
static char *normalize(const char *in)
{
    cJSON *root = cJSON_Parse(in);
    cJSON *pages = cJSON_GetObjectItem(root, "pages");
    if (!cJSON_IsArray(pages)) {
        cJSON_Delete(root);
        return nullptr;
    }
    cJSON *out = cJSON_CreateObject();
    cJSON *opages = cJSON_AddArrayToObject(out, "pages");
    for (int p = 0; p < layout_store::PAGES; p++) {
        cJSON *page = cJSON_GetArrayItem(pages, p);
        cJSON *op = cJSON_CreateArray();
        cJSON_AddItemToArray(opages, op);
        int n = 0;
        cJSON *w;
        cJSON_ArrayForEach(w, page)
        {
            cJSON *t = cJSON_GetObjectItem(w, "t");
            if (!cJSON_IsString(t) || n >= 16) continue;
            auto geti = [&](const char *k, int lo, int hi, int def) {
                cJSON *v = cJSON_GetObjectItem(w, k);
                int x = cJSON_IsNumber(v) ? v->valueint : def;
                return x < lo ? lo : x > hi ? hi : x;
            };
            int x = geti("x", 0, layout_store::COLS - 1, 0);
            int y = geti("y", 0, layout_store::ROWS - 1, 0);
            int ww = geti("w", 1, layout_store::COLS - x, 2);
            int hh = geti("h", 1, layout_store::ROWS - y, 2);
            cJSON *ow = cJSON_CreateObject();
            char tbuf[16];
            strncpy(tbuf, t->valuestring, sizeof(tbuf) - 1);
            tbuf[sizeof(tbuf) - 1] = 0;
            cJSON_AddStringToObject(ow, "t", tbuf);
            cJSON_AddNumberToObject(ow, "x", x);
            cJSON_AddNumberToObject(ow, "y", y);
            cJSON_AddNumberToObject(ow, "w", ww);
            cJSON_AddNumberToObject(ow, "h", hh);
            cJSON *txt = cJSON_GetObjectItem(w, "txt");
            if (cJSON_IsString(txt) && txt->valuestring[0]) {
                char tb[40];
                strncpy(tb, txt->valuestring, sizeof(tb) - 1);
                tb[sizeof(tb) - 1] = 0;
                cJSON_AddStringToObject(ow, "txt", tb);
            }
            cJSON_AddItemToArray(op, ow);
            n++;
        }
    }
    char *s = cJSON_PrintUnformatted(out);
    cJSON_Delete(out);
    cJSON_Delete(root);
    if (s && strlen(s) >= MAX_JSON) {
        free(s);
        return nullptr;
    }
    return s;
}

namespace layout_store {

void load()
{
    nvs_handle_t h;
    char *loaded = nullptr;
    if (nvs_open("axeforge", NVS_READONLY, &h) == ESP_OK) {
        size_t len = 0;
        if (nvs_get_str(h, "layout", NULL, &len) == ESP_OK && len > 2) {
            char *buf = (char *) malloc(len);
            nvs_get_str(h, "layout", buf, &len);
            loaded = normalize(buf);
            free(buf);
        }
        nvs_close(h);
    }
    s_json = loaded ? loaded : normalize(DEFAULT_LAYOUT);
}

const char *json() { return s_json; }

bool set_json(const char *in)
{
    char *n = normalize(in);
    if (!n) return false;
    nvs_handle_t h;
    if (nvs_open("axeforge", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "layout", n);
        nvs_commit(h);
        nvs_close(h);
    }
    // the UI only parses the string when it rebuilds, which happens after the
    // dirty flag below is seen, so the old copy can go now
    char *old = s_json;
    s_json = n;
    free(old);
    s_dirty = true;
    ESP_LOGI(TAG, "layout updated");
    return true;
}

bool take_dirty()
{
    bool d = s_dirty;
    s_dirty = false;
    return d;
}

} // namespace layout_store
