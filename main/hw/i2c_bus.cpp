#include "i2c_bus.h"

#include "driver/i2c.h"
#include "core/board_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define I2C_PORT I2C_NUM_0
#define I2C_SDA AF_PIN_I2C_SDA
#define I2C_SCL AF_PIN_I2C_SCL
#define I2C_HZ 100000
#define I2C_TIMEOUT pdMS_TO_TICKS(100)

static SemaphoreHandle_t s_lock;

namespace {
struct Lock {
    Lock() { xSemaphoreTake(s_lock, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(s_lock); }
};
}

namespace i2c_bus {

esp_err_t init()
{
    if (s_lock) return ESP_OK;
    s_lock = xSemaphoreCreateMutex();

    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_SDA;
    conf.scl_io_num = I2C_SCL;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = I2C_HZ;
    esp_err_t err = i2c_param_config(I2C_PORT, &conf);
    if (err != ESP_OK) return err;
    return i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0);
}

bool probe(uint8_t addr)
{
    Lock l;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(I2C_PORT, cmd, I2C_TIMEOUT);
    i2c_cmd_link_delete(cmd);
    return err == ESP_OK;
}

esp_err_t read(uint8_t addr, uint8_t reg, uint8_t *data, size_t len)
{
    Lock l;
    return i2c_master_write_read_device(I2C_PORT, addr, &reg, 1, data, len, I2C_TIMEOUT);
}

esp_err_t write(uint8_t addr, const uint8_t *data, size_t len)
{
    Lock l;
    return i2c_master_write_to_device(I2C_PORT, addr, data, len, I2C_TIMEOUT);
}

esp_err_t write_byte(uint8_t addr, uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = {reg, value};
    return write(addr, buf, 2);
}

esp_err_t read_word_le(uint8_t addr, uint8_t reg, uint16_t *value)
{
    uint8_t buf[2] = {0, 0};
    esp_err_t err = read(addr, reg, buf, 2);
    if (err == ESP_OK) *value = (uint16_t) (buf[0] | (buf[1] << 8));
    return err;
}

esp_err_t write_word_le(uint8_t addr, uint8_t reg, uint16_t value)
{
    uint8_t buf[3] = {reg, (uint8_t) (value & 0xff), (uint8_t) (value >> 8)};
    return write(addr, buf, 3);
}

esp_err_t send_cmd(uint8_t addr, uint8_t c)
{
    return write(addr, &c, 1);
}

} // namespace i2c_bus
