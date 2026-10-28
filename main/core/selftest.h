#pragma once
#include "cJSON.h"

// A hardware check that runs alongside mining (nothing is switched off): sensors,
// regulator, chips, fans, then a one-minute hashrate and error measurement.
namespace selftest {
// false if one is already running
bool start();
// {"running":bool,"done":bool,"passed":bool,"steps":[{"name","state","detail"}]}
// state: "wait", "run", "pass", "warn" or "fail"
cJSON *json();
}
