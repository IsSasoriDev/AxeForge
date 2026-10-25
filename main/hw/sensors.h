#pragma once
#include <stdint.h>

// EMC2302 dual fan controller (0x2E) and TMP1075 temperature sensors (0x48+).
namespace fan {
bool init();
void set_pct(int channel, float pct);  // 0..100
uint16_t rpm(int channel);
// Re-initialises the controller if its config registers were reset (a supply
// glitch does that); returns false if it had to.
bool check();
}

namespace temp {
// Number of ASIC-side TMP1075 sensors found (the layout is per board, see sensors.cpp).
int detect();
int count();
// Board sensor under ASIC area `index`, in C. NAN on error.
float asic_area(int index);
float vr_board();  // NAN on error
}
