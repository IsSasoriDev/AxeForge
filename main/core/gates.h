#pragma once
#include <stdint.h>

// The limits the tuner and the manual settings work within (#35, #33).
//
// Stock profile: the board's own limits (Kconfig defaults per board, with the
// run-time variants: rev7, TPS53667, NerdQX without its mux). What a tuned
// board stays inside.
//
// Envelope: the stock profile plus the board's known overclock headroom
// ([envelope] in boards/<id>.toml: power_pct, mv, freq_mhz). A board with no
// known headroom has no envelope, and can't be overclocked.
//
// Overclock, once unlocked with a typed confirmation, moves the tuner's power
// ceiling, the voltage limit and the clock limit (manual values too) from the
// stock profile to the envelope. Temperature ceilings and every emergency cut-off
// (115 % of the stock power, the critical temperatures) never move. A weak power
// supply turns overclocking off until it recovers.
namespace gates {

// the power cut-off sits at 115 % of the stock limit: an envelope's ceiling stays clear of it
constexpr int MAX_POWER_PCT = 10;

bool available();  // this board has headroom above stock
bool active();     // unlocked, available and the supply is fine

uint16_t max_freq();  // MHz
uint16_t max_mv();
float max_power();    // W, the tuner's ceiling

// the board file's [envelope], as compiled (0 where the board has none or can't use it)
uint16_t env_freq();
uint16_t env_mv();
uint8_t env_power_pct();

uint16_t stock_max_freq();
uint16_t stock_max_mv();
float stock_max_power();

}
