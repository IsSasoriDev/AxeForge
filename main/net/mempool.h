#pragma once

// Polls a mempool.space-compatible API for recently mined blocks, projected
// next blocks and recommended fees. Fills g_state.mempool.
namespace mempool {
void start();
void refresh_now();
}
