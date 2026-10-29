#include "http_util.h"

#include <stdlib.h>
#include <string.h>
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "http";

namespace http {

char *get(const char *url, size_t max_len, int timeout_ms, bool quiet)
{
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms = timeout_ms;
    cfg.buffer_size = 2048;
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {  // the URL did not parse or has no host
        if (!quiet) ESP_LOGW(TAG, "invalid URL: %s", url);
        return NULL;
    }
    esp_http_client_set_header(c, "Accept-Encoding", "identity");
    char *buf = NULL;
    if (esp_http_client_open(c, 0) == ESP_OK) {
        esp_http_client_fetch_headers(c);
        int status = esp_http_client_get_status_code(c);
        if (status == 200) {
            buf = (char *) heap_caps_malloc(max_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!buf) buf = (char *) malloc(max_len);
            size_t total = 0;
            int n;
            while (buf && total < max_len - 1 && (n = esp_http_client_read(c, buf + total, max_len - 1 - total)) > 0)
                total += n;
            if (buf) buf[total] = 0;
        } else {
            // never the query: it may carry a token
            if (!quiet) ESP_LOGW(TAG, "HTTP %d for %.*s", status, (int) strcspn(url, "?"), url);
        }
    } else {
        if (!quiet) ESP_LOGW(TAG, "request failed: %.*s", (int) strcspn(url, "?"), url);
    }
    esp_http_client_cleanup(c);
    return buf;
}

} // namespace http
