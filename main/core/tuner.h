#pragma once
#include <stdint.h>

// Auto-tuner: climbs frequency (adding voltage when errors appear) until it
// hits an error, thermal or power wall, then settles on the best stable point
// and trims voltage back down for lower temps. Keeps watching afterwards and
// backs off if the point drifts unstable (e.g. the room warms up).
namespace tuner {
void start();
void restart();  // forget the saved point and search again
// After a fault retry at a lower clock: neither the tuner nor manual mode goes
// above `mhz` again until the settings change or a re-tune.
void hold(uint16_t mhz);
}
