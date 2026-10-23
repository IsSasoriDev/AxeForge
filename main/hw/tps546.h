#pragma once
#include <stdint.h>

// TI TPS546D24 (NerdQaxe++ rev7). PMBus at 0x24. Two stacked phases feed the
// two ASIC voltage domains in series, so the regulator output is twice the
// per-chip core voltage. Everything here takes and returns the core voltage.
namespace tps546 {

constexpr float CORE_MIN = 1.0f;
constexpr float CORE_MAX = 1.5f;

bool detect();               // IC_DEVICE_ID matches a TPS546D24A/S
bool init();                 // run-time config (RAM only, never NVM), output left off
bool set_core(float volts);  // sets VOUT and switches the output on
bool off();                  // PMBus OPERATION off
void clear_faults();

float temperature();  // regulator die, NAN on error
float vin();
float iout();
float vout();  // per-domain core voltage
float pout();

uint8_t status_byte();
uint8_t status_iout();
uint8_t status_vout();
uint8_t status_input();
uint8_t status_temp();

} // namespace tps546
