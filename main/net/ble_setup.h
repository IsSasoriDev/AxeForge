#pragma once

// Wi-Fi setup over Bluetooth LE while the miner is in setup mode, with
// Espressif's standard provisioning protocol: the free "ESP BLE Provisioning"
// app (Android / iOS) finds the miner as PROV_AXEFORGE_XXXX, lists the Wi-Fi
// networks it sees and sends the one you pick. Encrypted (security 1); on a miner
// that already has Wi-Fi saved, the app asks for the setup network's password as
// its PIN (proof of possession), on first setup it asks for none.
// Bluetooth is shut down and its memory released once Wi-Fi is set up.
namespace ble_setup {
void start(const char *pop);  // from the Wi-Fi code when the setup network opens; pop may be null
bool active();
const char *name();  // advertised name, "" when off
}
