#pragma once
#include <stdint.h>

// TI TPS53647 multiphase buck (NerdQaxe family), PMBus at 0x71. Also drives its
// 6-phase sibling, the TPS53667 (NerdOctaxe-γ rev3.0+), told apart by device code.
namespace tps53647 {

constexpr float VOUT_MIN = 0.8f;
constexpr float VOUT_MAX = 1.4f;

bool detect();
bool is_tps53667();  // valid after detect()
bool init(int phases, int imax_a, float ifault_a);
bool set_vout(float volts);  // 0 is ignored; use the EN pin to switch off
void clear_faults();

float temperature();
float vin();
float iin();
float pin();
float vout();
float iout();

uint8_t status_byte();
uint8_t status_iout();
uint8_t status_vout();
uint8_t status_input();
uint8_t status_temp();

} // namespace tps53647
