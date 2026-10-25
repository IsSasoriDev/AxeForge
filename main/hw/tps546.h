#pragma once
#include <stdint.h>

// TI TPS546D24 at PMBus 0x24. The ASICs sit in voltage domains wired in
// series, so the regulator output is `domains` times the per-chip core
// voltage. Everything here takes and returns the core voltage.
namespace tps546 {

struct Profile {
    const char *name;
    int domains;
    float vin_on, vin_off, vin_uv_warn, vin_ov_fault;
    float scale_loop;
    float core_min, core_max;  // per domain
    float oc_warn_a, oc_fault_a;
    float ov_warn_ratio;
    uint8_t on_off_config;
    bool stacked;               // two-phase stack: STACK/SYNC/compensation are written
    uint16_t pin_detect_override;
};

// NerdQaxe++ rev7: two stacked TPS546s, 2 domains
extern const Profile NERDQAXE_REV7;
// Bitaxe Hex / Supra Hex: one TPS546, 3 domains of 2 chips
extern const Profile BITAXE_HEX;

bool detect();                     // IC_DEVICE_ID matches a TPS546D24A/S
bool init(const Profile &profile); // run-time config (RAM only, never NVM), output left off
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
