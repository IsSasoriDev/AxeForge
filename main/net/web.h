#pragma once

// Tiny HTTP server: setup page at "/", JSON at /api/status,
// POST /api/settings, /api/retune, /api/restart.
namespace web {
void start();
}
