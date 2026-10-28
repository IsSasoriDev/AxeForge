#pragma once
#include <stdint.h>

#include "core/board_config.h"

// BM13xx chain driver over UART1 (TX=17, RX=18). The chip model is fixed per
// board at compile time: BM1370 on the NerdQaxe++, BM1366 on the NerdAxe,
// BM1368 on the Supra Hex.
// Both speak the same framing and job format; they differ in chip ID, PLL
// range, init sequence and which register holds the hash counter.

struct __attribute__((packed)) AsicJob {
    uint8_t job_id;
    uint8_t num_midstates;
    uint8_t starting_nonce[4];
    uint8_t nbits[4];
    uint8_t ntime[4];
    uint8_t merkle_root[32];
    uint8_t prev_block_hash[32];
    uint8_t version[4];
};

struct AsicResult {
    bool is_register;
    // nonce result
    uint8_t job_slot;         // index into the job table (asic job id)
    uint32_t nonce;
    uint32_t version_bits;    // rolled header bits 13-28 (absolute, not a delta)
    int chip;
    // register result
    uint8_t reg;
    uint32_t data;
};

namespace asic {

#if AF_ASIC_BM1366
constexpr const char *MODEL = "BM1366";
constexpr int SMALL_CORES = 894;
constexpr uint8_t REG_NONCE_COUNT = 0x8C;  // total hash counter, 2^32 hashes per count
#elif AF_ASIC_BM1368
constexpr const char *MODEL = "BM1368";
constexpr int SMALL_CORES = 1276;
constexpr uint8_t REG_NONCE_COUNT = 0x8C;  // total hash counter, 2^32 hashes per count
#elif AF_ASIC_BM1373
constexpr const char *MODEL = "BM1373";
// 6912 is what the Nexus S1's stock firmware reports; its measured hashrate at
// 450 MHz works out to 6880-6900 (bitaxe's 6725 is 2.4% low).
constexpr int SMALL_CORES = 6912;
// 0x90 gives the right per-chip rates on a Nexus S1 (bitaxe reads 0x8C)
constexpr uint8_t REG_NONCE_COUNT = 0x90;
#else
constexpr const char *MODEL = "BM1370";
constexpr int SMALL_CORES = 2040;
constexpr uint8_t REG_NONCE_COUNT = 0x90;
#endif
constexpr uint8_t REG_HW_ERROR_COUNT = 0x4C;

void uart_init();
// Runs the full init sequence; returns number of chips that answered.
int init(float freq_mhz, int expected_chips, uint32_t difficulty);
void switch_to_fast_baud();
// Ramps in 6.25 MHz steps to avoid PLL glitches.
bool set_frequency(float freq_mhz);
float frequency();
void set_difficulty_mask(uint32_t difficulty);
// Restricts version rolling to the pool's BIP-310 mask.
void set_version_mask(uint32_t mask);
// BM1370 version-rolling speed in Hz (0 = the board's default). Kept for every
// later init; `write` also sends it now (only while the chips are up). False on
// other chips.
bool set_vr_freq(uint32_t hz, bool write);

// Per-chip clocks: each chip may run some MHz above (or below) the chain clock.
// Not on single-chip boards, and not on the BM1373 (untested addressing).
#if AF_NUM_ASICS > 1 && !AF_ASIC_BM1373
constexpr bool PER_CHIP_CLOCK = true;
#else
constexpr bool PER_CHIP_CLOCK = false;
#endif
// Stores the offsets (AF_NUM_ASICS entries); `write` also ramps the chips to them
// now. Every later set_frequency() / init() keeps them.
void set_chip_offsets(const int8_t *mhz, bool write);
int chip_offset(int chip);  // what the chip runs at now, relative to the chain
int offset_total();         // sum over the detected chips, for the expected hashrate

// Returns the asic job id (slot) used.
uint8_t send_job(uint32_t counter, AsicJob &job);
// Blocks up to timeout_ms for one 11-byte response.
bool receive(AsicResult &out, int timeout_ms);

void read_register_all(uint8_t reg);
void reset_register_all(uint8_t reg);

} // namespace asic
