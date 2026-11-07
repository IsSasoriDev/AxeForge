#include "fake_tps546.h"

#include <string.h>

#include <string>

#include "hw/i2c_bus.h"

std::string g_esp_log;

namespace fake_tps546 {
std::function<bool(const Write &)> refuse;
std::vector<Write> writes;
std::vector<Write> attempts;
uint8_t vout_mode = 0x97;  // relative limits, exponent -9, as the TPS546D24A reports

static const uint8_t DEVICE_ID[6] = {0x54, 0x49, 0x54, 0x6D, 0x24, 0x41};  // TPS546D24A
static std::map<uint8_t, std::vector<uint8_t>> s_regs;

void reset()
{
    refuse = nullptr;
    writes.clear();
    attempts.clear();
    s_regs.clear();
    vout_mode = 0x97;
    g_esp_log.clear();
}

static const Write *last(uint8_t cmd)
{
    for (auto it = writes.rbegin(); it != writes.rend(); ++it)
        if (it->cmd == cmd) return &*it;
    return nullptr;
}

int last_word(uint8_t cmd)
{
    const Write *w = last(cmd);
    if (!w || w->data.size() != 2) return -1;
    return w->data[0] | (w->data[1] << 8);
}

int last_byte(uint8_t cmd)
{
    const Write *w = last(cmd);
    if (!w || w->data.size() != 1) return -1;
    return w->data[0];
}

bool written(uint8_t cmd) { return last(cmd) != nullptr; }

bool attempted(uint8_t cmd)
{
    for (const Write &w : attempts)
        if (w.cmd == cmd) return true;
    return false;
}
}

namespace i2c_bus {
using namespace fake_tps546;

esp_err_t init() { return ESP_OK; }
bool probe(uint8_t addr) { return addr == 0x24; }

esp_err_t read(uint8_t addr, uint8_t reg, uint8_t *data, size_t len)
{
    if (addr != 0x24) return ESP_FAIL;
    memset(data, 0, len);
    if (reg == 0xAD) {  // IC_DEVICE_ID, SMBus block: length first
        data[0] = 6;
        memcpy(data + 1, DEVICE_ID, len - 1 < 6 ? len - 1 : 6);
    } else if (reg == 0x20) {  // VOUT_MODE
        data[0] = vout_mode;
    } else {
        auto it = s_regs.find(reg);
        if (it != s_regs.end()) memcpy(data, it->second.data(), it->second.size() < len ? it->second.size() : len);
    }
    return ESP_OK;
}

esp_err_t write(uint8_t addr, const uint8_t *data, size_t len)
{
    if (addr != 0x24 || len == 0) return ESP_FAIL;
    Write w{data[0], std::vector<uint8_t>(data + 1, data + len)};
    attempts.push_back(w);
    if (refuse && refuse(w)) return ESP_FAIL;
    writes.push_back(w);
    s_regs[w.cmd] = w.data;
    return ESP_OK;
}

esp_err_t write_byte(uint8_t addr, uint8_t reg, uint8_t value)
{
    uint8_t b[2] = {reg, value};
    return write(addr, b, 2);
}

esp_err_t read_word_le(uint8_t addr, uint8_t reg, uint16_t *value)
{
    uint8_t b[2];
    esp_err_t e = read(addr, reg, b, 2);
    if (e == ESP_OK) *value = (uint16_t) (b[0] | (b[1] << 8));
    return e;
}

esp_err_t write_word_le(uint8_t addr, uint8_t reg, uint16_t value)
{
    uint8_t b[3] = {reg, (uint8_t) value, (uint8_t) (value >> 8)};
    return write(addr, b, 3);
}

esp_err_t send_cmd(uint8_t addr, uint8_t c) { return write(addr, &c, 1); }
}
