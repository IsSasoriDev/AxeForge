#pragma once
#include "sdkconfig.h"

// Heatbit Bitair: the purifier around the board tells it which fan mode the user
// picked on the top panel, as a PWM duty on GPIO40 (CN1 pin 8). Off (and a signal
// that can't be read) means no mining; the other modes set a floor under the fan.
// The front LED on GPIO41 (CN1 pin 6) shows the mode, as the stock firmware does.
// Ported from skot's Bitair support in ESP-Miner (the `bitair` branch of
// github.com/skot/esp-miner-skot), which matched the stock firmware's thresholds.
namespace purifier {

enum class Mode { Unknown, Off, Silent, Auto, Turbo };

#if CONFIG_AF_BOARD_BITAIR
void start();          // input capture and the LED task; before the first power-up
Mode mode();
const char *mode_name();
bool want_off();       // off or unreadable: chips stay off
float fan_floor_pct(); // fan duty the selected mode asks for, at least
#else
inline void start() {}
inline Mode mode() { return Mode::Unknown; }
inline const char *mode_name() { return ""; }
inline bool want_off() { return false; }
inline float fan_floor_pct() { return 0; }
#endif

} // namespace purifier
