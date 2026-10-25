#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

// Board I2C (pins in core/board_config.h), 100 kHz. All access is serialized by a mutex
// so the monitor task and tuner can share the bus.
namespace i2c_bus {
esp_err_t init();
bool probe(uint8_t addr);
esp_err_t read(uint8_t addr, uint8_t reg, uint8_t *data, size_t len);
esp_err_t write(uint8_t addr, const uint8_t *data, size_t len);
esp_err_t write_byte(uint8_t addr, uint8_t reg, uint8_t value);
// SMBus word = little endian
esp_err_t read_word_le(uint8_t addr, uint8_t reg, uint16_t *value);
esp_err_t write_word_le(uint8_t addr, uint8_t reg, uint16_t value);
esp_err_t send_cmd(uint8_t addr, uint8_t cmd);
}
