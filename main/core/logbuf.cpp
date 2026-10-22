#include "logbuf.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#define RING_SIZE (32 * 1024)

static char *s_ring;
static uint32_t s_head = 0;  // absolute bytes ever written
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static vprintf_like_t s_orig;

static void append(const char *s, size_t n)
{
    portENTER_CRITICAL(&s_mux);
    for (size_t i = 0; i < n; i++) s_ring[(s_head + i) % RING_SIZE] = s[i];
    s_head += n;
    portEXIT_CRITICAL(&s_mux);
}

static int hook(const char *fmt, va_list args)
{
    va_list copy;
    va_copy(copy, args);
    char line[200];  // logging runs on every task's stack, keep this small
    int n = vsnprintf(line, sizeof(line), fmt, copy);
    va_end(copy);
    if (n > 0 && s_ring) {
        if (n >= (int) sizeof(line)) {
            n = sizeof(line) - 1;
            line[n - 1] = '\n';
        }
        // drop ANSI colour sequences (ESC [ ... m), in place
        int o = 0;
        for (int i = 0; i < n; i++) {
            if (line[i] == 0x1b) {
                while (i < n && line[i] != 'm') i++;
                continue;
            }
            line[o++] = line[i];
        }
        append(line, o);
    }
    return s_orig ? s_orig(fmt, args) : n;
}

namespace logbuf {

void init()
{
    s_ring = (char *) heap_caps_malloc(RING_SIZE, MALLOC_CAP_SPIRAM);
    if (!s_ring) s_ring = (char *) malloc(RING_SIZE / 4);
    s_orig = esp_log_set_vprintf(hook);
}

uint32_t read(uint32_t since, char *out, size_t max)
{
    portENTER_CRITICAL(&s_mux);
    uint32_t head = s_head;
    uint32_t oldest = head > RING_SIZE ? head - RING_SIZE : 0;
    if (since < oldest || since > head) since = oldest;
    size_t n = head - since;
    if (n > max - 1) {
        since = head - (max - 1);
        n = max - 1;
    }
    for (size_t i = 0; i < n; i++) out[i] = s_ring[(since + i) % RING_SIZE];
    portEXIT_CRITICAL(&s_mux);
    out[n] = 0;
    return head;
}

} // namespace logbuf
