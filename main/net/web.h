#pragma once

// Tiny HTTP server: setup page at "/", JSON at /api/status,
// POST /api/settings, /api/retune, /api/restart.
namespace web {
void start();
// What asked for the last software restart ("POST /api/restart from 172.16.0.5"),
// kept across the restart in RTC memory. Empty if the restart did not come from here.
const char *previous_restart();
}
