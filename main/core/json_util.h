#pragma once
#include <stddef.h>

#include "cJSON.h"

// cJSON parses recursively and allows 1000 levels of nesting, while the tasks
// that parse untrusted JSON (pools, Swarm peers, web handlers, weather/mempool
// replies) run on 6-12 KB stacks: a line of ~1000 '[' overflows them. Every
// JSON from outside goes through json_parse(), which refuses anything nested
// deeper than this before cJSON sees it. Nothing AxeForge reads needs more than 6.
#define JSON_MAX_DEPTH 16

// Deepest bracket nesting in `s`, ignoring brackets inside strings. Stops
// counting (and returns a value above `limit`) as soon as `limit` is passed.
inline int json_depth(const char *s, int limit)
{
    int depth = 0, max = 0;
    bool in_str = false;
    for (; *s; s++) {
        char c = *s;
        if (in_str) {
            if (c == '\\' && s[1]) s++;  // skip the escaped character
            else if (c == '"') in_str = false;
            continue;
        }
        if (c == '"') in_str = true;
        else if (c == '[' || c == '{') {
            if (++depth > max) max = depth;
            if (max > limit) return max;
        } else if (c == ']' || c == '}') {
            if (depth > 0) depth--;
        }
    }
    return max;
}

// cJSON_Parse for untrusted input: NULL (as for any parse error) when nested
// deeper than JSON_MAX_DEPTH.
inline cJSON *json_parse(const char *s)
{
    if (!s || json_depth(s, JSON_MAX_DEPTH) > JSON_MAX_DEPTH) return nullptr;
    return cJSON_Parse(s);
}
