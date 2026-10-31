#include "gates.h"

#include "core/board.h"
#include "core/board_config.h"
#include "core/psu.h"
#include "core/settings.h"
#include "sdkconfig.h"

#ifndef AF_ENV_POWER_PCT
#define AF_ENV_POWER_PCT 0
#endif
#ifndef AF_ENV_MV
#define AF_ENV_MV 0
#endif
#ifndef AF_ENV_FREQ_MHZ
#define AF_ENV_FREQ_MHZ 0
#endif
// the power cut-off sits at 115 % of the stock limit: the ceiling stays clear of it
#define MAX_POWER_PCT 10
#define ABS_MAX_MV 1500  // the top of the Kconfig voltage range

namespace gates {

bool available()
{
    // rev7 stacks its chips in series and has its own limits: no known margin
    return (AF_ENV_POWER_PCT > 0 || AF_ENV_MV > 0 || AF_ENV_FREQ_MHZ > 0) && !board::is_rev7();
}

bool active()
{
    if (!settings::get().overclock || !available()) return false;
    psu::Level l = psu::state().level;
    return l != psu::Level::Weak && l != psu::Level::Bad;
}

uint16_t stock_max_freq() { return board::limits().max_freq_mhz; }
uint16_t stock_max_mv() { return board::max_mv(); }
float stock_max_power() { return board::limits().max_power_w; }

uint16_t max_freq() { return stock_max_freq() + (active() ? AF_ENV_FREQ_MHZ : 0); }

uint16_t max_mv()
{
    int mv = stock_max_mv() + (active() ? AF_ENV_MV : 0);
    return (uint16_t) (mv > ABS_MAX_MV ? ABS_MAX_MV : mv);
}

float max_power()
{
    int pct = AF_ENV_POWER_PCT > MAX_POWER_PCT ? MAX_POWER_PCT : AF_ENV_POWER_PCT;
    return stock_max_power() * (active() ? 1.0f + pct / 100.0f : 1.0f);
}

}
