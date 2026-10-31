#pragma once
#include "cJSON.h"

// What the miner costs to run and what it buys: energy used today and in total,
// its cost at the user's electricity price (or the market price from the price
// mode when none is set), and the odds of finding a block at this hashrate.
namespace energy {
void load();
// Once a second from the monitor task (writes NVS once an hour: internal stack only).
void tick();
void reset_totals();
cJSON *json();
}
