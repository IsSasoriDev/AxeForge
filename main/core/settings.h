#pragma once
#include <stddef.h>
#include <stdint.h>

#include "core/board_config.h"  // AF_NUM_ASICS

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
    char hostname[33] = "";       // empty: stock firmware's name, else axeforge-XXXX

    PoolCfg pools[AF_MAX_POOLS];
    PoolMode pool_mode = PoolMode::Split;

    uint16_t freq_mhz = 600;     // stock NerdQaxe++ point
    uint16_t core_mv = 1150;
    bool autotune = true;
    // last stable point found by the tuner, reused on boot
    uint16_t tuned_freq = 0;
    uint16_t tuned_mv = 0;
    // what the tuner looks for: 0 = most hashrate, 1 = most hashrate under
    // power_cap_w, 2 = best efficiency (J/TH)
    uint8_t tune_goal = 0;
    // overclock unlocked (typed confirmation in the UI): the tuner and the manual
    // values may use the board's envelope instead of its stock limits (core/gates.h)
    bool overclock = false;
    uint16_t power_cap_w = 0;      // 0 = the board's limit
    // per-chip clocks: once locked, the tuner raises each chip on its own;
    // chip_off is each chip's offset (MHz) from the chain clock
    bool chip_tune = true;
    int8_t chip_off[AF_NUM_ASICS] = {};

    uint8_t fan_target_c = 60;   // PID target for hottest chip
    uint8_t fan_min_pct = 30;
    // per fan header: 0 = auto (temperature loop), 1 = manual duty
    uint8_t fan_mode[2] = {0, 0};
    uint8_t fan_manual[2] = {60, 60};
    uint8_t theme = 0;             // 0 = Forge (dark neon), 1 = Paper (e-ink style), 2 = Ember (Bitcoin orange), 3 = Spatial (deep space)
    uint8_t brightness = 100;
    uint8_t screen_off_min = 0;   // backlight off after this many idle minutes, 0 = never

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

    // solo payout check: 0 = warn only, 1 = don't mine on a pool whose coinbase
    // doesn't pay the address in its username (or keeps more than payout_max_fee %)
    uint8_t payout_mode = 0;
    uint8_t payout_max_fee = 100;  // percent; 100 = no fee limit

    // advanced chip timing, 0 = the board's default
    uint16_t job_ms = 0;           // new job for the chips every N ms
    uint32_t vr_hz = 0;            // BM1370 version-rolling speed (Hz)

    // notifications
    char discord_url[160] = "";    // https://discord.com/api/webhooks/...
    char tg_token[64] = "";        // Telegram bot token
    char tg_chat[24] = "";         // Telegram chat id
    uint8_t notify_events = 0x3f;  // NOTIFY_* bits

    bool bap_on = true;            // Bitaxe accessory port
    bool ble_setup = true;         // Bluetooth Wi-Fi setup while in setup mode

    // after a web update, go back to the previous firmware if the new one
    // crashes at start-up or doesn't mine within 10 minutes
    bool ota_guard = true;

    // electricity price: stop mining while the price is above price_max
    bool price_on = false;
    uint8_t price_src = 0;         // 0 = Energy-Charts day-ahead price, 1 = your own URL
    char price_zone[12] = "DE-LU"; // Energy-Charts bidding zone
    char price_url[128] = "";      // returns JSON with a number (or numeric string)
    char price_path[40] = "";      // dotted path to it, e.g. "state" or "data.0.price"
    float price_max = 15;          // ct/kWh for Energy-Charts, the URL's own unit otherwise

    // what the electricity costs, for the power bill card: per kWh in `currency`;
    // 0 = use the market price from the price mode, if that is on
    float energy_price = 0;
    char currency[5] = "€";
};

// notify_events bits
#define NOTIFY_BLOCK 0x01
#define NOTIFY_BEST 0x02
#define NOTIFY_FAULT 0x04
#define NOTIFY_POOL 0x08
#define NOTIFY_BOOT 0x10
#define NOTIFY_MAINTENANCE 0x20  // clean the heatsink

namespace settings {
void load();
void save();
Settings &get();
// pools are stored as one JSON blob
char *pools_to_json();  // caller frees
void pools_from_json(const char *json);
// A consistent copy of a pool's username (the web server may be rewriting it).
void pool_user(int pool, char *out, size_t len);
// 1-32 letters, digits and hyphens, not starting or ending with a hyphen
bool valid_hostname(const char *h);
// The worker part of a pool user ("address.worker" -> "worker"), or "".
void worker_of(const char *user, char *out, size_t len);
// This miner's worker name on the pool it is mining for.
void own_worker(char *out, size_t len);
}
