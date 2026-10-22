#pragma once
#include <stddef.h>
#include <stdint.h>

// Captures all ESP_LOG output into a RAM ring buffer (still printed to USB)
// so the web UI can show a live console.
namespace logbuf {
void init();
// Copies log text written at or after absolute offset `since` into out
// (at most max-1 bytes, NUL terminated). Returns the offset to ask for next.
uint32_t read(uint32_t since, char *out, size_t max);
}
