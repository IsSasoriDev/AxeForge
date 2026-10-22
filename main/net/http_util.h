#pragma once
#include <stddef.h>

namespace http {
// GET over HTTP(S) with the built-in CA bundle. Returns a NUL-terminated heap
// buffer (caller frees) or NULL on failure/non-200.
char *get(const char *url, size_t max_len);
}
