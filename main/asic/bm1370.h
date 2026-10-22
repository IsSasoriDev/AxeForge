#pragma once
#include <stdint.h>

// BM1370 chain driver over UART1 (TX=17, RX=18).

struct __attribute__((packed)) Bm1370Job {
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
    uint32_t version_bits;    // rolled bits, OR with job version
    int chip;
    // register result
    uint8_t reg;
    uint32_t data;
};

namespace bm1370 {

constexpr int SMALL_CORES = 2040;
constexpr uint8_t REG_NONCE_COUNT = 0x90;
constexpr uint8_t REG_HW_ERROR_COUNT = 0x4C;

void uart_init();
// Runs the full init sequence; returns number of chips that answered.
int init(float freq_mhz, int expected_chips, uint32_t difficulty);
void switch_to_fast_baud();
// Ramps in 6.25 MHz steps to avoid PLL glitches.
bool set_frequency(float freq_mhz);
float frequency();
void set_difficulty_mask(uint32_t difficulty);

// Returns the asic job id (slot) used.
uint8_t send_job(uint32_t counter, Bm1370Job &job);
// Blocks up to timeout_ms for one 11-byte response.
bool receive(AsicResult &out, int timeout_ms);

void read_register_all(uint8_t reg);
void reset_register_all(uint8_t reg);

} // namespace bm1370
