#pragma once
#include "cJSON.h"

// STA mode with saved credentials; falls back to an open setup AP
// ("AxeForge-XXXX", 192.168.4.1) when none are saved or the connection fails.
namespace wifi {
void start();
bool connected();
bool in_setup_mode();
// Ethernet came up or went down: the setup network closes while a cable gives an
// address, and the shown IP follows whichever link is up.
void eth_changed();
// Networks in range, strongest first: [{"ssid","rssi","secure"}]. Blocks ~2-3 s.
cJSON *scan();
}
