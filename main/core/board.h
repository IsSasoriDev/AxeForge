#pragma once
#include <stdint.h>

// NerdQaxe++ (4x BM1370, TPS53647 3-phase buck) power and telemetry.
// Every voltage/frequency change in the firmware goes through here so the
// hard limits from Kconfig are enforced in exactly one place.
namespace board {

enum class Error { None, NoRegulator, UnsupportedRev7, NoChips, PsuFault, OverCurrent, VoutFault, VrOverTemp };

Error init_hardware();                  // I2C, fans, sensors, GPIOs
Error power_up(uint16_t freq_mhz, uint16_t mv);
void shutdown();                         // core voltage off, LDO off, ASIC reset

bool set_core_mv(uint16_t mv);
bool set_freq_mhz(uint16_t mhz);
uint16_t core_mv();
uint16_t freq_mhz();
bool is_powered();

uint16_t clamp_mv(int mv);
uint16_t clamp_freq(int mhz);

void read_telemetry();                   // fills g_state power/thermal fields
Error check_faults();
const char *error_text(Error e);

} // namespace board
