#pragma once

// ICMP ping to the pool in use, so the UI can tell a network problem (round
// trip, loss) apart from a slow pool (share response time). Results land in
// g_state.net_*.
namespace netping {
void start();
// Stop pinging (and free the session's task) while a firmware update installs
void pause(bool on);
}
