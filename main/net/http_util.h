#pragma once
#include <stddef.h>

namespace http {
// GET over HTTP(S) with the built-in CA bundle. Returns a NUL-terminated heap
// buffer (caller frees) or NULL on failure/non-200. `quiet` skips the warning
// log, for callers that expect misses (swarm polling of offline miners).
char *get(const char *url, size_t max_len, int timeout_ms = 8000, bool quiet = false);
}
