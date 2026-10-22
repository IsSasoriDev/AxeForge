#pragma once

// 1 Hz telemetry, fan PID and the emergency cut-off. Runs independently of
// the tuner so a stuck tuner can never stop the thermal protection.
namespace monitor {
void start();
}
