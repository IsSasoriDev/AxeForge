#pragma once
#include <stdint.h>

// Power supply check: how far the input voltage drops under load. A supply (or
// barrel jack, or cable) that is about to cause faults shows up here first.
namespace psu {

enum class Level : uint8_t { Unknown, Ok, Weak, Bad };

struct State {
    float nominal = 0;   // 5 or 12 V, from the first readings
    float idle_v = 0;    // with the chips off
    float load_v = 0;    // last minute's average under load
    float min_v = 0;     // lowest one-second reading under load since boot
    float load_w = 0;    // input power over the same minute
    float sag_pct = 0;   // load_v below nominal, percent
    float ohms = 0;      // (idle_v - load_v) / current: supply + cable resistance
    Level level = Level::Unknown;
};

// Once a second from the monitor task.
void tick();
const State &state();
const char *level_name(Level l);

}
