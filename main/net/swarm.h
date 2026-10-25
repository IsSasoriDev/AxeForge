#pragma once

// Swarm: the other miners on the LAN, polled by this miner so the browser
// never has to talk to them (keeps the API guard strict, no CORS needed).
// Understands the AxeOS / ESP-Miner-NerdQAxePlus API (/api/system/info) and
// AxeForge's own (/api/status). The list is kept in NVS.
namespace swarm {
void start();
char *to_json();               // caller frees
bool add(const char *ip);      // false if not a valid IPv4 address or the list is full
bool remove(const char *ip);
void request_scan();           // probes the local /24 for miners, in the background
}
