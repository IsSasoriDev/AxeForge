#pragma once
// Compile-time description of the target board. One firmware image per board:
// the pinouts overlap, and a pin that is an LCD data line on one board can be a
// regulator enable on another.

#include "sdkconfig.h"

#if CONFIG_AF_BOARD_NERDAXE
#define AF_BOARD_NAME "NerdAxe"
#define AF_BOARD_ID "nerdaxe"
#define AF_NUM_ASICS 1
#define AF_ASIC_BM1366 1
#define AF_PIN_I2C_SDA 44
#define AF_PIN_I2C_SCL 43
#define AF_HAS_LCD 1  // the ESP32 is a LilyGo T-Display-S3
#define AF_HAS_VR_SENSOR 0  // nothing reads the TPS40305
#elif CONFIG_AF_BOARD_SUPRAHEX
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

// Embedded in every image so a firmware upload for another board is refused
// (their pinouts disagree about which level switches the core power on).
#define AF_BOARD_TAG_PREFIX "AXEFORGE-BOARD:"
#define AF_BOARD_TAG AF_BOARD_TAG_PREFIX AF_BOARD_ID ";"

#ifndef AF_HAS_VR_SENSOR
#define AF_HAS_VR_SENSOR 1
#endif

// Both boards share these
#define AF_PIN_ASIC_RST 1
#define AF_PIN_UART_TX 17
#define AF_PIN_UART_RX 18
