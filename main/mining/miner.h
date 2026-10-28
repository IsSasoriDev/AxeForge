#pragma once

// Job dispatch, result handling and hashrate measurement.
namespace miner {
void start();
void pause(bool paused);   // stop feeding jobs (used while changing freq/voltage)
void kick();               // new clean job available: dispatch now
void forget_pool(int pool); // pool disconnected: drop results for its jobs
// Clears the hashrate/error measurement window (tuner calls this after a change).
void reset_window();
// Average over the current window; returns seconds covered.
float window_stats(float *ghs, float *err_pct);
// Highest per-chip error rate (percent) over the current window.
// `chip` (optional) gets its index, -1 when no chip has errors.
float window_worst_chip_err(int *chip = nullptr);
// One chip's error rate (percent) over the current window.
float window_chip_err(int chip);
}
