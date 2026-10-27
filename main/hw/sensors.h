#pragma once
#include <stdint.h>

// Fans and temperature sensors, per board (see sensors.cpp):
//  - NerdQaxe family and Supra Hex: EMC2302 dual fan controller, TMP1075 board sensors
//  - NerdAxe, Bitaxe Supra and Gamma: EMC2101 (one fan, board sensor, chip diode)
//  - Bitaxe Naja Duo: EMC2103 (one fan, board sensor, two chip diodes)
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
// The chip's own temperature diode, read by the fan controller (Bitaxe boards).
// NAN on boards without one, on a read error, or while the chip has no power
// (an unpowered diode reads open).
float chip_diode(int index);
}
