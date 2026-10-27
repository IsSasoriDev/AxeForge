#pragma once
#include <stdbool.h>

// The 5 V Bitaxe power path (Ultra/Supra 400-401, NerdAxe): a TPS40305 buck whose
// output is trimmed by a DS4432U current DAC (0x48), input measured by an INA260
// (0x40), core voltage read back on ADC1 channel 1. The buck enable pin belongs
// to the board code.
namespace ds4432u {
bool present();               // the DAC answers
void init();                  // ADC; harmless to call more than once
bool set_vout(float volts);   // false if out of the buck's range or the write failed
// INA260 readings; false (and the value untouched) on a bus error
bool vin(float &v);
bool iin(float &a);
bool pin(float &w);
float vcore();  // measured core voltage, NAN if the ADC can't tell
}
