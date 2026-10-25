#include "captive.h"

#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include "net/wifi.h"

static const char *TAG = "captive";

#define DNS_PORT 53
#define AP_IP {192, 168, 4, 1}
#define DNS_TTL_S 60

// Builds the reply in place: same ID and question, one A record pointing at
// the AP for A/ANY queries, an empty answer for everything else (AAAA etc.).
static int build_reply(uint8_t *buf, int len, int cap)
{
    if (len < 12) return -1;
    if (buf[2] & 0x80) return -1;  // a response, not a query
    uint16_t qd = (buf[4] << 8) | buf[5];
    if (qd != 1) return -1;

    // walk the question name
    int p = 12;
    while (p < len && buf[p]) {
        if ((buf[p] & 0xC0) == 0xC0) return -1;  // no compression in questions
        p += buf[p] + 1;
    }
    p++;  // terminating zero
    if (p + 4 > len) return -1;
    uint16_t qtype = (buf[p] << 8) | buf[p + 1];
    p += 4;  // QTYPE + QCLASS
    bool answer = qtype == 1 || qtype == 255;

    buf[2] = 0x84 | (buf[2] & 0x01);  // QR, AA, keep RD
    buf[3] = 0x80;                    // RA, NOERROR
    buf[6] = 0;
    buf[7] = answer ? 1 : 0;          // ANCOUNT
    buf[8] = buf[9] = buf[10] = buf[11] = 0;
    if (!answer) return p;

    if (p + 16 > cap) return -1;
    static const uint8_t ip[4] = AP_IP;
    uint8_t *a = buf + p;
    a[0] = 0xC0; a[1] = 0x0C;  // pointer to the question name
    a[2] = 0; a[3] = 1;        // A
    a[4] = 0; a[5] = 1;        // IN
    a[6] = 0; a[7] = 0; a[8] = 0; a[9] = DNS_TTL_S;
    a[10] = 0; a[11] = 4;
    memcpy(a + 12, ip, 4);
    return p + 16;
}

static void dns_task(void *)
{
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) {
        ESP_LOGE(TAG, "socket failed");
        vTaskDelete(NULL);
    }
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(DNS_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (sockaddr *) &addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "bind :53 failed");
        close(s);
        vTaskDelete(NULL);
    }
    uint8_t buf[512];
    while (true) {
        sockaddr_in from;
        socklen_t fl = sizeof(from);
        int n = recvfrom(s, buf, sizeof(buf), 0, (sockaddr *) &from, &fl);
        if (n <= 0 || !wifi::in_setup_mode()) continue;
        int out = build_reply(buf, n, sizeof(buf));
        if (out > 0) sendto(s, buf, out, 0, (sockaddr *) &from, fl);
    }
}

namespace captive {
void start() { xTaskCreatePinnedToCore(dns_task, "dns", 3072, NULL, 3, NULL, 0); }
}
