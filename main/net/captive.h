#pragma once

// Captive portal for the setup AP: a DNS responder that answers every A query
// with 192.168.4.1, so phones and laptops pop up the setup page on their own.
// It stays silent outside setup mode. The matching HTTP redirect for unknown
// paths (connectivity checks) lives in web.cpp.
namespace captive {
void start();
}
