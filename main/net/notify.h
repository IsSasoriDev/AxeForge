#pragma once
#include <stdint.h>

// Discord / Telegram messages for the events picked in the settings
// (NOTIFY_* bits). Sending happens on its own task; post() never blocks.
namespace notify {
void start();
// Queues a message if `event` is enabled. Safe from any task.
void post(uint8_t event, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
// Sends a test message to every configured channel; returns false if none is set.
bool test();
// "https://discord.com/api/webhooks/..." (or discordapp.com)
bool valid_discord(const char *url);
bool valid_tg_token(const char *token);
}
