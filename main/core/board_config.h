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
#define AF_JOB_INTERVAL_MS 1500  // NerdAxe reference cadence
// one small chip: a lower ticket difficulty keeps results coming every few seconds
#define AF_ASIC_DIFF_MIN 64
#define AF_ASIC_DIFF_MAX 256
#define AF_KEEP_STOCK_NVS 1
#elif CONFIG_AF_BOARD_SUPRAHEX
#define AF_BOARD_NAME "Bitaxe Supra Hex"
#define AF_BOARD_ID "suprahex"
#define AF_NUM_ASICS 6
#define AF_ASIC_BM1368 1
#define AF_PIN_I2C_SDA 47
#define AF_PIN_I2C_SCL 48
#define AF_HAS_LCD 0  // 128x32 SSD1306 OLED instead
#define AF_JOB_INTERVAL_MS (500 / AF_NUM_ASICS)  // same cadence as the reference firmware
#define AF_KEEP_STOCK_NVS 1
#elif CONFIG_AF_BOARD_NEXUS
// BitLuck Nexus S1, board version 1010. Pins read from the stock firmware
// (nexus.v1.0.2); the layout is the NerdQaxe++'s apart from the power pins.
#define AF_BOARD_NAME "Nexus S1"
#define AF_BOARD_ID "nexus"
#define AF_NUM_ASICS 4
#define AF_ASIC_BM1373 1
#define AF_PIN_I2C_SDA 44
#define AF_PIN_I2C_SCL 43
#define AF_HAS_LCD 1  // T-Display-S3 panel, same pins as the NerdQaxe++
#define AF_ASIC_DIFF_MIN 1024  // about three BM1370s' worth of hashing per chip (shufps' Q1373 values)
#define AF_ASIC_DIFF_MAX 4096
#define AF_KEEP_STOCK_NVS 1
// its VR area runs ~67 C at stock clocks (the stock firmware's fans hold it there at
// ~68%); a 65 C target kept fan 2 pinned at 100%
#define AF_VR_FAN_TARGET_C 72.0f
#else
// The NerdQaxe family: same ESP32 module, LCD, fans (EMC2302), board sensors
// (TMP1075) and TPS536x7 regulator wiring; they differ in chips, phases and limits.
#define AF_NQ_FAMILY 1
#define AF_PIN_I2C_SDA 44
#define AF_PIN_I2C_SCL 43
#define AF_HAS_LCD 1
#if CONFIG_AF_BOARD_NERDQAXEPLUS
#define AF_BOARD_NAME "NerdQAxe+"
#define AF_BOARD_ID "nerdqaxeplus"
#define AF_NUM_ASICS 4
#define AF_ASIC_BM1368 1
#define AF_VR_PHASES 2
#define AF_INIT_MV 1250
#define AF_JOB_INTERVAL_MS 1200
#define AF_ASIC_DIFF_MIN 256
#define AF_ASIC_DIFF_MAX 1024
#elif CONFIG_AF_BOARD_NERDOCTAXEPLUS
#define AF_BOARD_NAME "NerdOctaxe+"
#define AF_BOARD_ID "nerdoctaxeplus"
#define AF_NUM_ASICS 8
#define AF_ASIC_BM1368 1
#define AF_VR_PHASES 3
#define AF_INIT_MV 1250
#define AF_JOB_INTERVAL_MS 1200
#elif CONFIG_AF_BOARD_NERDOCTAXEGAMMA
#define AF_BOARD_NAME "NerdOctaxe-\xCE\xB3"
#define AF_BOARD_ID "nerdoctaxegamma"
#define AF_NUM_ASICS 8
#define AF_ASIC_BM1370 1
#define AF_VR_PHASES 4  // TPS53647; a TPS53667 (rev3.0+, GPIO3 strapped high) runs 6
#define AF_VR_IMAX_A 180
#define AF_VR_IFAULT_A 160
#define AF_INIT_MV 0    // power up straight at the target voltage
#define AF_DETECT_TPS53667 1
#define AF_TMP451_MUXES 2  // per-chip die temperature, ASICs 0-3 at 0x4c, 4-7 at 0x4e
#define AF_TMP451_PIN_A1 12
#define AF_PROXY_DIE_OFFSET 10.0f  // older boards without the muxes: board sensor + 10 C
#define AF_ASIC_DIFF_MIN 1024
#define AF_ASIC_DIFF_MAX 4096
#elif CONFIG_AF_BOARD_NERDQX
#define AF_BOARD_NAME "NerdQX"
#define AF_BOARD_ID "nerdqx"
#define AF_NUM_ASICS 4
#define AF_ASIC_BM1370 1
#define AF_VR_PHASES 3
#define AF_VR_IFAULT_A 142  // reference firmware value
#define AF_TMP451_MUXES 1  // per-chip die temperature at 0x4c
#define AF_TMP451_PIN_A1 3
#define AF_PROXY_DIE_OFFSET 10.0f
#define AF_VR_FREQ_HZ 35000  // version rolling speed for the higher clocks
#define AF_ASIC_DIFF_MIN 1024
#define AF_ASIC_DIFF_MAX 4096
// Without the TMP451 mux this is not a QX: stay at NerdQaxe++ stock levels.
#define AF_NO_MUX_MAX_FREQ 495
#define AF_NO_MUX_MAX_MV 1150
#else
#define AF_BOARD_NAME "NerdQaxe++"
#define AF_BOARD_ID "nerdqaxe"
#define AF_NUM_ASICS 4
#define AF_ASIC_BM1370 1
#define AF_VR_PHASES 3
#define AF_DETECT_REV7 1  // rev7 swaps the TPS53647 for a TPS546 stack
#endif
#if !CONFIG_AF_BOARD_NERDQAXE
#define AF_KEEP_STOCK_NVS 1  // new to AxeForge: take over the stock firmware's settings
#endif
#ifndef AF_VR_IMAX_A
#define AF_VR_IMAX_A (AF_VR_PHASES * 30)
#endif
#ifndef AF_VR_IFAULT_A
#define AF_VR_IFAULT_A (AF_VR_IMAX_A + 5)
#endif
#ifndef AF_INIT_MV
#define AF_INIT_MV 1200
#endif
#endif

// Embedded in every image so a firmware upload for another board is refused
// (their pinouts disagree about which level switches the core power on).
#define AF_BOARD_TAG_PREFIX "AXEFORGE-BOARD:"
#define AF_BOARD_TAG AF_BOARD_TAG_PREFIX AF_BOARD_ID ";"

#ifndef AF_HAS_VR_SENSOR
#define AF_HAS_VR_SENSOR 1
#endif
#ifndef AF_VR_FAN_TARGET_C
#define AF_VR_FAN_TARGET_C 65.0f  // the VR fan's PI target
#endif
#ifndef AF_JOB_INTERVAL_MS
#define AF_JOB_INTERVAL_MS 500
#endif
#ifndef AF_ASIC_DIFF_MIN
#define AF_ASIC_DIFF_MIN 512
#define AF_ASIC_DIFF_MAX 2048
#endif
#ifndef AF_TMP451_MUXES
#define AF_TMP451_MUXES 0
#endif
#ifndef AF_PROXY_DIE_OFFSET
#define AF_PROXY_DIE_OFFSET 0.0f
#endif

// All boards share these
#define AF_PIN_ASIC_RST 1
#define AF_PIN_UART_TX 17
#define AF_PIN_UART_RX 18
