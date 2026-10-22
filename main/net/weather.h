#pragma once
#include <stdbool.h>
#include <time.h>

// SNTP time + Open-Meteo weather (no API key). The city from settings is
// geocoded once; the forecast call also returns the local UTC offset, which
// the clock uses so no timezone setting is needed.
namespace weather {
void start();
void refresh_now();                 // settings changed: re-geocode and fetch
bool local_time(struct tm *out);    // false until SNTP has synced
const char *describe(int wmo_code);
}
