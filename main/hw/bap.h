#pragma once

// Bitaxe Accessory Port: the AxeOS serial protocol on the Bitaxe's accessory
// header (TX GPIO39, RX GPIO40, 115200 8N1), so displays and other add-ons made
// for AxeOS work with AxeForge too. Messages look like
//   $BAP,<CMD>,<parameter>,<value>*<XOR checksum>\r\n
// Supported: REQ, SUB/UNSUB (periodic RES updates) and SET for frequency,
// asic_voltage, fan_speed, auto_fan, ssid and password. The Wi-Fi password is
// never sent out, unlike AxeOS.
namespace bap {
void start();  // no-op on boards without the header
}
