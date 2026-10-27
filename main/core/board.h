#pragma once
#include <stdint.h>

// NerdQaxe++ (4x BM1370) power and telemetry. Rev <= 6 boards use a TPS53647
// 3-phase buck, rev7 a stacked TPS546D24; the revision is detected at boot.
// Every voltage/frequency change in the firmware goes through here so the
// hard limits from Kconfig are enforced in exactly one place.
namespace board {

enum class Error { None, NoRegulator, NoTempSensor, NoChips, ChipMissing, PsuFault, OverCurrent, VoutFault, VrOverTemp };

// Limits that depend on the board revision (chip temperature limits do not).
struct Limits {
    uint16_t max_freq_mhz;
    float max_power_w;
    float vr_ceil_c;  // tuner stops climbing
    float vr_crit_c;  // emergency cut-off
};
const Limits &limits();
bool is_rev7();

Error init_hardware();                  // I2C, fans, sensors, GPIOs
Error power_up(uint16_t freq_mhz, uint16_t mv);
void shutdown();                         // core voltage off, LDO off, ASIC reset

bool set_core_mv(uint16_t mv);
bool set_freq_mhz(uint16_t mhz);
uint16_t core_mv();
uint16_t freq_mhz();
bool is_powered();

uint16_t clamp_mv(int mv);
uint16_t max_mv();  // the Kconfig limit, or lower where the board needs it (NerdQX without die sensors)
uint16_t clamp_freq(int mhz);

void read_telemetry();                   // fills g_state power/thermal fields
Error check_faults();
const char *error_text(Error e);

} // namespace board
