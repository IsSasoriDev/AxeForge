#pragma once
#include <stdint.h>

// EMC2302 dual fan controller (0x2E) and TMP1075 temperature sensors (0x48+).
namespace fan {
bool init();
void set_pct(int channel, float pct);  // 0..100
uint16_t rpm(int channel);
}

namespace temp {
// Number of ASIC-side TMP1075 sensors found (the one at 0x49 is the VR sensor).
int detect();
int count();
// Board sensor under ASIC area `index`, in C, 0 on error.
float asic_area(int index);
float vr_board();
}
