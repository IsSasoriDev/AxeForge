#pragma once
// A TPS546 on a fake I2C bus, for testing main/hw/tps546.cpp on the host.
// It remembers every write and can refuse (NACK) chosen ones, as a real part
// does when it rejects a command or a value.
#include <stdint.h>

#include <functional>
#include <map>
#include <vector>

namespace fake_tps546 {
struct Write {
    uint8_t cmd;
    std::vector<uint8_t> data;  // bytes after the command
};

// Return true to NACK this write.
extern std::function<bool(const Write &)> refuse;
extern std::vector<Write> writes;    // accepted
extern std::vector<Write> attempts;  // accepted or refused
extern uint8_t vout_mode;

void reset();
// The last value written to a word command, or -1 if it was never written.
int last_word(uint8_t cmd);
int last_byte(uint8_t cmd);
bool written(uint8_t cmd);
bool attempted(uint8_t cmd);
}
