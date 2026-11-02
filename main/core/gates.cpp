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
#define ABS_MAX_MV 1500  // the top of the Kconfig voltage range

namespace gates {

static bool has_env() { return AF_ENV_POWER_PCT > 0 || AF_ENV_MV > 0 || AF_ENV_FREQ_MHZ > 0; }

bool available()
{
    // rev7 stacks its chips in series and has its own limits: no known margin
    return has_env() && !board::is_rev7();
}

bool active()
{
    if (!settings::get().overclock || !available()) return false;
    psu::Level l = psu::state().level;
    return l != psu::Level::Weak && l != psu::Level::Bad;
}

uint16_t env_freq() { return available() ? AF_ENV_FREQ_MHZ : 0; }
uint16_t env_mv() { return available() ? AF_ENV_MV : 0; }
uint8_t env_power_pct()
{
    if (!available()) return 0;
    return AF_ENV_POWER_PCT > MAX_POWER_PCT ? MAX_POWER_PCT : AF_ENV_POWER_PCT;
}

uint16_t stock_max_freq() { return board::limits().max_freq_mhz; }
uint16_t stock_max_mv() { return board::max_mv(); }
float stock_max_power() { return board::limits().max_power_w; }

uint16_t max_freq() { return stock_max_freq() + (active() ? env_freq() : 0); }

uint16_t max_mv()
{
    int mv = stock_max_mv() + (active() ? env_mv() : 0);
    return (uint16_t) (mv > ABS_MAX_MV ? ABS_MAX_MV : mv);
}

float max_power()
{
    return stock_max_power() * (active() ? 1.0f + env_power_pct() / 100.0f : 1.0f);
}

}
