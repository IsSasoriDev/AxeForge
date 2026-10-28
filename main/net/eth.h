#pragma once

// Wired Ethernet through the W5500 adapter that plugs into the NerdQAxe-family
// header (SCLK GPIO2, MOSI 12, MISO 16, CS 21; reset by RC, no interrupt line),
// the same one the stock NerdQAxe firmware supports. Found at boot by reading
// the chip's version register; with no adapter nothing else happens.
// Runs next to Wi-Fi: whichever has an address carries the traffic.
namespace eth {
void start();
bool up();          // link up and an address from DHCP
const char *ip();   // "" when not up
}
