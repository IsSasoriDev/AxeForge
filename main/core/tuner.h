#pragma once

// Auto-tuner: climbs frequency (adding voltage when errors appear) until it
// hits an error, thermal or power wall, then settles on the best stable point
// and trims voltage back down for lower temps. Keeps watching afterwards and
// backs off if the point drifts unstable (e.g. the room warms up).
namespace tuner {
void start();
void restart();  // forget the saved point and search again
}
