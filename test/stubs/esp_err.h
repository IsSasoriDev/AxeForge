#pragma once
// Host stand-in for ESP-IDF's esp_err.h: only what the drivers under test use.
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_TIMEOUT 0x107

static inline const char *esp_err_to_name(esp_err_t e)
{
    switch (e) {
    case ESP_OK: return "ESP_OK";
    case ESP_ERR_INVALID_STATE: return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_TIMEOUT: return "ESP_ERR_TIMEOUT";
    default: return "ESP_FAIL";
    }
}
