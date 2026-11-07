#pragma once
#include <stdint.h>

// 1 Hz telemetry, fan PID and the emergency cut-off. Runs independently of
// the tuner so a stuck tuner can never stop the thermal protection.
namespace monitor {
void start();
// Before start(): the chips were left off on purpose at boot (Bitair panel off);
// the monitor powers them up once nothing wants them off.
void start_paused();
// The last emergency cut-off with its readings, kept across software restarts
// and crashes (not a power cut). Empty if there was none.
const char *last_emergency();
uint32_t emergency_count();  // since the last power-on
}
