#pragma once
// Host stand-in for ESP-IDF's esp_log.h. Every line also goes into g_esp_log so
// a test can check what a driver reported.
#include <stdio.h>

#include <string>

extern std::string g_esp_log;

#define AF_HOST_LOG(level, tag, fmt, ...)                                          \
    do {                                                                           \
        char af_line_[256];                                                        \
        snprintf(af_line_, sizeof(af_line_), "%s %s: " fmt "\n", level, tag, ##__VA_ARGS__); \
        g_esp_log += af_line_;                                                     \
    } while (0)

#define ESP_LOGE(tag, fmt, ...) AF_HOST_LOG("E", tag, fmt, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) AF_HOST_LOG("W", tag, fmt, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) AF_HOST_LOG("I", tag, fmt, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) AF_HOST_LOG("D", tag, fmt, ##__VA_ARGS__)
