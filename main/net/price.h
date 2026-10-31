#pragma once
#include <stdint.h>

#include "cJSON.h"

// Electricity price: mining stops while the price is above the user's limit.
// Prices come from Energy-Charts (day-ahead market, by bidding zone, no key) or
// from any URL that returns a number in JSON. This task only fetches; the monitor
// powers the chips down and up, so the fault handling stays in one place.
namespace price {
void start();
void refresh_now();
// True while mining should stop for the price.
bool want_pause();
// The current Energy-Charts market price in ct/kWh, NAN when the price mode is off,
// uses a URL, or has no price for this moment.
float market_ct_kwh();
// {"on","src","now","max","unit","pausing","updated","error","slots":[...]} for the web UI.
cJSON *json();
}
