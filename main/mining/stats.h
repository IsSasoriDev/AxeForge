#pragma once
#include <stdint.h>

#include "cJSON.h"

// Best shares (kept in flash) and the 24-hour statistics behind the web charts.
namespace stats {

#define AF_TOP_SHARES 10

struct TopShare {
    double diff = 0;
    uint32_t time = 0;    // unix time if the clock was synced, else 0
    uint32_t uptime = 0;  // seconds since boot when it was found
    int8_t pool = -1;
    int8_t chip = -1;
};

// Loads the saved scoreboard and seeds g_state.best_diff from it.
void load();
// A result the chips found: kept if it makes the top list. Cheap, called for
// every nonce; flash is written later by tick().
void offer(double diff, int pool, int chip);
void reset();
cJSON *scoreboard_json();

// Called once a minute from the counter task: adds a sample to the 24-hour
// series and writes a changed scoreboard to flash.
void tick();
// {"step":300,"hashrate":[...],"temp":[...],"power":[...]}, oldest first
cJSON *day_json();

}
