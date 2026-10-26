#pragma once
#include <stdint.h>
#include "mining/work.h"

// Up to AF_MAX_POOLS pool connections, each running Stratum V1 (plain or TLS)
// or Stratum V2 (Noise-encrypted, extended channel). Every protocol is
// normalized into the same StratumJob template so the miner treats them alike.
namespace pools {

struct JobInfo {
    char en1[67];          // extranonce1 hex (empty for SV2: folded into coinb1)
    int en2_len;
    uint32_t version_mask;
    double difficulty;     // the pool's share difficulty, fraction included
};

void start();

// True when the pool is connected and has a job the chips can use.
bool ready(int pool);

// Copies the pool's current template if its generation differs from *gen.
// coinb1/coinb2 in `out` are heap copies owned by the caller afterwards.
bool fetch_job(int pool, StratumJob &out, JobInfo &info, uint32_t *gen);
uint32_t generation(int pool);
bool take_clean_flag(int pool);

// Thread-safe; called from the result task.
void submit(int pool, const Work &w, uint32_t nonce, uint32_t version);

// Restart connections after settings changed.
void reconfigure();

} // namespace pools
