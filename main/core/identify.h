#pragma once
#include <stdint.h>

#include "esp_timer.h"

// "Find this miner": for a while the screen blinks (the LCD backlight, the OLED
// inverting, the Bitair's front LED) so the owner can tell which box on the shelf
// is the one open in the browser. The display tasks poll this; nothing else changes.
namespace identify {

inline volatile int64_t g_until_us = 0;

#define IDENTIFY_BLINK_US 400000  // on 400 ms, off 400 ms

inline void start(int seconds) { g_until_us = esp_timer_get_time() + (int64_t) seconds * 1000000; }
inline void stop() { g_until_us = 0; }
inline bool active() { return esp_timer_get_time() < g_until_us; }
inline int remaining_s()
{
    int64_t left = g_until_us - esp_timer_get_time();
    return left > 0 ? (int) ((left + 999999) / 1000000) : 0;
}
// the blink's phase: true = lit
inline bool lit() { return (esp_timer_get_time() / IDENTIFY_BLINK_US) % 2 == 0; }

} // namespace identify
