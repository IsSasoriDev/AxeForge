#pragma once
#include <stdint.h>

#define AF_MAX_POOLS 4

enum class PoolProto : uint8_t { V1 = 0, V1Tls = 1, V2 = 2 };
enum class PoolMode : uint8_t { Split = 0, Failover = 1 };

struct PoolCfg {
    bool enabled = false;
    char url[64] = "";
    uint16_t port = 3333;
    char user[96] = "";
    char pass[32] = "x";
    PoolProto proto = PoolProto::V1;
    uint8_t weight = 100;     // split mode: share of work in percent
    char sv2_key[64] = "";    // optional SV2 authority pubkey (base58)
};

// Persistent user settings (NVS namespace "axeforge").
struct Settings {
    char wifi_ssid[33] = "";
    char wifi_pass[65] = "";

    PoolCfg pools[AF_MAX_POOLS];
    PoolMode pool_mode = PoolMode::Split;

    uint16_t freq_mhz = 600;     // stock NerdQaxe++ point
    uint16_t core_mv = 1150;
    bool autotune = true;
    // last stable point found by the tuner, reused on boot
    uint16_t tuned_freq = 0;
    uint16_t tuned_mv = 0;

    uint8_t fan_target_c = 60;   // PID target for hottest chip
    uint8_t fan_min_pct = 30;
    uint8_t brightness = 100;

    // display
    uint8_t display_type = 0;      // 0 = 3.5" 480x320, 1 = LilyGo T-Display-S3 320x170
    char pages[12] = "dcbpxs";     // page order, see ui.cpp PAGE_* letters
    uint8_t default_page = 0;      // index into pages
    uint8_t page_rotate_s = 0;     // 0 = off, else seconds per page
    bool rotate_180 = false;

    // clock & weather
    char city[48] = "";
    float lat = 0, lon = 0;        // resolved from city, 0/0 = unset
    bool fahrenheit = false;
    bool clock_24h = true;

    // mempool.space (or a self-hosted instance)
    bool mempool_on = true;
    char mempool_url[64] = "https://mempool.space";
};

namespace settings {
void load();
void save();
Settings &get();
// pools are stored as one JSON blob
char *pools_to_json();  // caller frees
void pools_from_json(const char *json);
}
