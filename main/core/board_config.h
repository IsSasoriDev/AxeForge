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
#elif CONFIG_AF_BOARD_SUPRAHEX || CONFIG_AF_BOARD_ULTRAHEX
// The Hex boards (board_suprahex.cpp): 6 chips in 3 stacked domains on one
// TPS546, EMC2302 fans, two TMP1075s, 128x32 OLED. Only the chip differs.
#define AF_HEX_FAMILY 1
#define AF_NUM_ASICS 6
#define AF_PIN_I2C_SDA 47
#define AF_PIN_I2C_SCL 48
#define AF_HAS_LCD 0  // 128x32 SSD1306 OLED instead
#define AF_VOLTAGE_DOMAINS 3
#define AF_KEEP_STOCK_NVS 1
#if CONFIG_AF_BOARD_ULTRAHEX
#define AF_BOARD_NAME "Bitaxe Ultra Hex"
#define AF_BOARD_ID "ultrahex"
#define AF_ASIC_BM1366 1
#define AF_JOB_INTERVAL_MS (2000 / 8)  // reference firmware: BM1366 timeout / next power of two of the chips
#define AF_ASIC_DIFF_MIN 256
#define AF_ASIC_DIFF_MAX 1024
#else
#define AF_BOARD_NAME "Bitaxe Supra Hex"
#define AF_BOARD_ID "suprahex"
#define AF_ASIC_BM1368 1
#define AF_JOB_INTERVAL_MS (500 / AF_NUM_ASICS)  // same cadence as the reference firmware
#endif
#elif CONFIG_AF_BOARD_BITAXE_SUPRA || CONFIG_AF_BOARD_BITAXE_GAMMA || CONFIG_AF_BOARD_NAJA || \
    CONFIG_AF_BOARD_BITAXE_ULTRA || CONFIG_AF_BOARD_BITAXE_MAX || CONFIG_AF_BOARD_GAMMA_DUO || CONFIG_AF_BOARD_BITAXE_GT || CONFIG_AF_BOARD_GAMMA_HEX
// The other Bitaxes (board_bitaxe.cpp): one regulator, a TPS546 (or a stack of
// them), or on the Supra 400/401 and Ultra 201-205 the 5 V DS4432U path, found at
// boot. One EMC2101/EMC2103 for the fan and the chip diodes. Board numbers and
// values from the reference firmware's device_config.h.
#define AF_BITAXE_FAMILY 1
#define AF_KEEP_STOCK_NVS 1  // flashed over AxeOS: take over its Wi-Fi, pools and board settings
#if CONFIG_AF_BOARD_BITAXE_SUPRA
#define AF_BOARD_NAME "Bitaxe Supra"
#define AF_BOARD_ID "supra"
#define AF_NUM_ASICS 1
#define AF_ASIC_BM1368 1
#define AF_PIN_I2C_SDA 47
#define AF_PIN_I2C_SCL 48
#define AF_HAS_LCD 0  // 128x32 SSD1306 OLED
#define AF_ASIC_DIFF_MIN 128
#define AF_ASIC_DIFF_MAX 512
#define AF_HAS_DS4432U 1  // rev 400/401
#define AF_EMC2101 1
#elif CONFIG_AF_BOARD_BITAXE_ULTRA
#define AF_BOARD_NAME "Bitaxe Ultra"
#define AF_BOARD_ID "ultra"
#define AF_NUM_ASICS 1
#define AF_ASIC_BM1366 1
#define AF_PIN_I2C_SDA 47
#define AF_PIN_I2C_SCL 48
#define AF_HAS_LCD 0  // 128x32 SSD1306 OLED
#define AF_HAS_DS4432U 1  // rev 201-205; the 207 has a TPS546
#define AF_EMC2101 1
#define AF_JOB_INTERVAL_MS 1500  // one BM1366, as the NerdAxe
#define AF_ASIC_DIFF_MIN 64
#define AF_ASIC_DIFF_MAX 256
#elif CONFIG_AF_BOARD_BITAXE_MAX
#define AF_BOARD_NAME "Bitaxe Max"
#define AF_BOARD_ID "max"
#define AF_NUM_ASICS 1
#define AF_ASIC_BM1397 1
#define AF_PIN_I2C_SDA 47
#define AF_PIN_I2C_SCL 48
#define AF_HAS_LCD 0  // 128x32 SSD1306 OLED
#define AF_HAS_DS4432U 1
#define AF_DS4432U_DIODE 1  // the chip's diode is wired to the EMC2101 on the Max
#define AF_EMC2101 1
#define AF_JOB_INTERVAL_MS 30  // four midstates scan the whole space in ~40 ms at 425 MHz
#define AF_ASIC_DIFF_MIN 64
#define AF_ASIC_DIFF_MAX 256
#elif CONFIG_AF_BOARD_GAMMA_DUO
#define AF_BOARD_NAME "Bitaxe Gamma Duo"
#define AF_BOARD_ID "gammaduo"
#define AF_NUM_ASICS 2  // in parallel on one 5 V TPS546
#define AF_ASIC_BM1370 1
#define AF_PIN_I2C_SDA 47
#define AF_PIN_I2C_SCL 48
#define AF_HAS_LCD 0  // 128x32 SSD1306 OLED
#define AF_EMC2101 1  // one diode for both chips
#define AF_EMC_IDEALITY 0x24
#define AF_EMC_BETA 0x00
#define AF_ASIC_DIFF_MIN 256
#define AF_ASIC_DIFF_MAX 1024
#elif CONFIG_AF_BOARD_BITAXE_GT
#define AF_BOARD_NAME "Bitaxe GT"
#define AF_BOARD_ID "gt"
#define AF_NUM_ASICS 2  // in parallel on two stacked TPS546s, 12 V in
#define AF_ASIC_BM1370 1
#define AF_PIN_I2C_SDA 47
#define AF_PIN_I2C_SCL 48
#define AF_HAS_LCD 0  // 128x32 SSD1306 OLED
#define AF_EMC2103 1
#define AF_EMC_FLIP 1  // diode 2 is chip 1
#define AF_POWER_OFFSET_W 10.0f
#elif CONFIG_AF_BOARD_GAMMA_HEX
#define AF_BOARD_NAME "Bitaxe Gamma Hex"
#define AF_BOARD_ID "gammahex"
#define AF_NUM_ASICS 6
#define AF_ASIC_BM1370 1
#define AF_VOLTAGE_DOMAINS 2  // 3 chips per domain, four stacked TPS546s
#define AF_PIN_I2C_SDA 44     // "color" board, as the Naja: ST7789 LCD, I2C on 44/43
#define AF_PIN_I2C_SCL 43
#define AF_HAS_LCD 1
#define AF_COLOR_BOARD 1
#define AF_ASIC_EN_ACTIVE_HIGH 1
#define AF_EMC2103 1
#define AF_EMC_FLIP 1
#define AF_EMC_DIRECT_PWM 1
#define AF_POWER_OFFSET_W 25.0f
#define AF_ASIC_DIFF_MIN 1024
#define AF_ASIC_DIFF_MAX 4096
#elif CONFIG_AF_BOARD_BITAXE_GAMMA
#define AF_BOARD_NAME "Bitaxe Gamma"
#define AF_BOARD_ID "gamma"
#define AF_NUM_ASICS 1
#define AF_ASIC_BM1370 1
#define AF_PIN_I2C_SDA 47
#define AF_PIN_I2C_SCL 48
#define AF_HAS_LCD 0  // 128x32 SSD1306 OLED
#define AF_EMC_IDEALITY 0x24  // BM1370 diode model (reference firmware)
#define AF_EMC_BETA 0x00
#define AF_ASIC_DIFF_MIN 256
#define AF_ASIC_DIFF_MAX 1024
#define AF_EMC2101 1
#else
#define AF_BOARD_NAME "Bitaxe Naja Duo"
#define AF_BOARD_ID "naja"
#define AF_NUM_ASICS 2
#define AF_ASIC_BM1373 1
#define AF_VOLTAGE_DOMAINS 2  // the two chips are stacked
#define AF_PIN_I2C_SDA 44     // "color" board: T-Display-style LCD, I2C moved to 44/43
#define AF_PIN_I2C_SCL 43
#define AF_HAS_LCD 1          // ST7789 320x170, driven as the T-Display-S3 screen
#define AF_COLOR_BOARD 1      // no accessory (BAP) header
#define AF_ASIC_EN_ACTIVE_HIGH 1
#define AF_EMC2103 1
#define AF_POWER_OFFSET_W 0.0f
#define AF_EMC_IDEALITY 0x17
#define AF_EMC_BETA 0x10
#define AF_MIN_FREQ_MHZ 250   // the BM1373 runs around 330 MHz
#endif
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
#define AF_BM1373_SHUFPS_INIT 1  // init as the stock firmware does (shufps); the Naja uses bitaxe's
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
#define AF_HAS_LCD 1
#if CONFIG_AF_BOARD_HX6
// HX-6 (github.com/IsSasoriDev/HX6-Miner, rev B): open hardware with the
// NerdQaxe++'s power wiring (VR_EN 10, LDO_EN 13, reset 1, TPS53647 at 0x71,
// EMC2302 fans) and 6 BM1370s on one parallel rail. It differs in the I2C pins,
// three TMP1075s under the chips (none at the regulator), a 2.8in SPI ST7789 and
// its buttons. The W5500 header pins are the screen's here: no Ethernet.
#define AF_BOARD_NAME "HX-6"
#define AF_BOARD_ID "hx6"
#define AF_NUM_ASICS 6
#define AF_ASIC_BM1370 1
#define AF_PIN_I2C_SDA 8
#define AF_PIN_I2C_SCL 9
#define AF_VR_PHASES 4
#define AF_INIT_MV 0              // power up straight at the target voltage
#define AF_VR_TEMP_FROM_CONTROLLER 1  // the TPS53647's own sensor
#define AF_LCD_SPI_ST7789 1       // Waveshare 2.8in: SCK 12, MOSI 11, CS 7, DC 6, RST 5, BL 38
#define AF_PIN_BTN_1 21           // user button; GPIO14 is VR_RDY on this board
#define AF_NO_ETH 1
#define AF_ASIC_DIFF_MIN 1024
#define AF_ASIC_DIFF_MAX 4096
#elif CONFIG_AF_BOARD_NERDQAXEPLUS
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
#ifndef AF_PIN_I2C_SDA
#define AF_PIN_I2C_SDA 44
#define AF_PIN_I2C_SCL 43
#endif
#if !CONFIG_AF_BOARD_NERDQAXE && !CONFIG_AF_BOARD_HX6
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
#ifndef AF_VOLTAGE_DOMAINS
#define AF_VOLTAGE_DOMAINS 1
#endif
#ifndef AF_MIN_FREQ_MHZ
#define AF_MIN_FREQ_MHZ 400  // lowest clock the tuner and recoveries go to
#endif

// All boards share these
#define AF_PIN_ASIC_RST 1
#define AF_PIN_UART_TX 17
#define AF_PIN_UART_RX 18
