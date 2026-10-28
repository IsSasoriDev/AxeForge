#pragma once

// Wi-Fi setup over Bluetooth LE while the miner is in setup mode, with
// Espressif's standard provisioning protocol: the free "ESP BLE Provisioning"
// app (Android / iOS) finds the miner as PROV_AXEFORGE_XXXX, lists the Wi-Fi
// networks it sees and sends the one you pick. Encrypted (security 1), no PIN,
// the same openness as the setup Wi-Fi network that runs next to it.
// Bluetooth is shut down and its memory released once Wi-Fi is set up.
namespace ble_setup {
void start();      // from the Wi-Fi code when the setup network opens
bool active();
const char *name();  // advertised name, "" when off
}
