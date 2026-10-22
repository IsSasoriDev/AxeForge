#pragma once

// STA mode with saved credentials; falls back to an open setup AP
// ("AxeForge-XXXX", 192.168.4.1) when none are saved or the connection fails.
namespace wifi {
void start();
bool connected();
bool in_setup_mode();
}
