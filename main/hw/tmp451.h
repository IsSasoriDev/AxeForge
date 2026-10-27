#pragma once
#include <stdint.h>

// TMP451 remote-diode sensors behind a 4-channel analog mux: real die temperature
// for each chip (NerdQX: one mux, NerdOctaxe-γ: two sharing the select lines).
// A conversion takes a few hundred ms per chip, so a background task cycles the
// chips and the monitor reads the cached values.
namespace tmp451 {
// Probes the muxes (count from board_config.h) and starts the reader task.
// Returns the number of chips covered (0 = no mux fitted).
int start();
int chips();
// The diodes sit in the chips: readings only mean something while the chips are
// powered. Disabling drops all cached values (and any conversion in flight).
void enable(bool on);
// Last die temperature of chip `index` in C; NAN if never read or stale.
float chip(int index);
}
