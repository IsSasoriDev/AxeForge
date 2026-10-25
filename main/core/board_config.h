#pragma once
// Compile-time description of the target board. One firmware image per board:
// the pinouts overlap, and a pin that is an LCD data line on one board can be a
// regulator enable on another.

#include "sdkconfig.h"

#if CONFIG_AF_BOARD_SUPRAHEX
#define AF_BOARD_NAME "Bitaxe Supra Hex"
#define AF_BOARD_ID "suprahex"
#define AF_NUM_ASICS 6
#define AF_ASIC_BM1368 1
#define AF_PIN_I2C_SDA 47
#define AF_PIN_I2C_SCL 48
#define AF_HAS_LCD 0  // 128x32 SSD1306 OLED instead
#else
#define AF_BOARD_NAME "NerdQaxe++"
#define AF_BOARD_ID "nerdqaxe"
#define AF_NUM_ASICS 4
#define AF_ASIC_BM1370 1
#define AF_PIN_I2C_SDA 44
#define AF_PIN_I2C_SCL 43
#define AF_HAS_LCD 1
#endif

// Both boards share these
#define AF_PIN_ASIC_RST 1
#define AF_PIN_UART_TX 17
#define AF_PIN_UART_RX 18
