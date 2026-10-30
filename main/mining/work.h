#pragma once
#include <stddef.h>
#include <stdint.h>
#include "asic/bm13xx.h"
#include "core/state.h"

#define MAX_MERKLE_BRANCHES 32

// A mining.notify, already decoded to binary.
struct StratumJob {
    char job_id[40];
    uint8_t prev_hash_raw[32];   // as sent by the pool (word swapped)
    char *coinb1 = nullptr;      // hex, heap
    char *coinb2 = nullptr;      // hex, heap
    uint8_t merkle[MAX_MERKLE_BRANCHES][32];
    int n_merkle = 0;
    uint32_t version = 0;
    uint32_t nbits = 0;
    uint32_t ntime = 0;
    bool clean = false;
};

// One unit of work handed to the chips. Kept per asic job slot so results
// can be verified and submitted.
struct Work {
    bool valid = false;
    int pool = 0;
    char job_id[40];
    char extranonce2[33];
    uint32_t version;
    uint32_t version_mask;
    uint32_t ntime;
    uint32_t nbits;
    double pool_diff;
    uint8_t prev_hash[32];       // header order
    uint8_t merkle_root[32];     // header order
};

namespace work {

size_t hex2bin(const char *hex, uint8_t *bin, size_t bin_len);
void bin2hex(const uint8_t *bin, size_t len, char *hex);

// Builds the coinbase, merkle root and both the verification Work and the
// chip job. extranonce1 is hex.
void build(const StratumJob &sj, const char *extranonce1, uint32_t en2, int en2_len, uint32_t version_mask,
           double pool_diff, Work &w, AsicJob &chip_job);

// Returns share difficulty of a found nonce (0 = invalid hash).
// hash_out (optional) receives the 32-byte block hash in internal order.
double nonce_difficulty(const Work &w, uint32_t nonce, uint32_t version, uint8_t *hash_out = nullptr);

// Decodes what's being mined: height, reward, coinbase tag, prev hash, and the
// coinbase outputs. With `user` (the pool username) it also checks whether the
// coinbase pays the address in it (solo mining).
void describe_template(const StratumJob &sj, const char *extranonce1, int en2_len, BlockTemplate &out,
                       const char *user = nullptr);

double nbits_to_difficulty(uint32_t nbits);

// The next version inside the pool's rolling mask (carries across gaps in the
// mask, leaves the other bits alone). How the BM1397's midstates are numbered.
inline uint32_t next_version(uint32_t v, uint32_t mask) { return (v & ~mask) | (((v | ~mask) + 1u) & mask); }
uint32_t block_height_from_coinb1(const char *coinb1);

} // namespace work
