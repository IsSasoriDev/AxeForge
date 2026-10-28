#pragma once
#include "esp_partition.h"
#include "esp_system.h"

// Rollback for web updates that works with any bootloader (the stock Bitaxe ones
// have no app rollback). After an update the new image is on probation: it is
// kept once it has mined at a normal hashrate for two minutes, and the previous
// firmware boots again if it crashes at start-up twice, or if 10 minutes after
// boot it is online but not mining. No network means no verdict: a router
// outage is not the firmware's fault.
namespace ota_guard {
// At boot, after settings::load() and before the hardware comes up.
void boot(esp_reset_reason_t rr);
// After an update was written to `updated` and set to boot.
void armed(const esp_partition_t *updated);
// Once a second, from the monitor task (it writes NVS: internal stack only).
void tick();
bool pending();
// The user keeps the new firmware now.
void keep();
}
