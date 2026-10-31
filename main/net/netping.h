#pragma once

// ICMP ping to the pool in use, so the UI can tell a network problem (round
// trip, loss) apart from a slow pool (share response time). Results land in
// g_state.net_*.
namespace netping {
void start();
}
