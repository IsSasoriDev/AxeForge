#pragma once
#include <stdint.h>
#include "cJSON.h"

// Clean-heatsink reminder. Learns how warm the chips run and how hard the fan
// works at this miner's usual power over its first week, then compares every
// later week with that. Dust on the heatsink or fan shows up as the fan working
// harder for the same heat (or the chips running hotter once the fan is at full
// speed), with the outdoor temperature from the weather page taken out as well
// as it can be.
namespace dust {

enum class State : uint8_t { Learning, Ok, Dirty };

void load();  // at boot, after settings
// Once a minute from the monitor task (writes NVS once a day: internal stack only).
void tick_minute();
// The user cleaned it: forget the baseline and learn again.
void reset();
State state();
// {"state","days","baseTemp","temp","baseFan","fan","dTemp","dFan"} for the web UI
cJSON *json();

}
