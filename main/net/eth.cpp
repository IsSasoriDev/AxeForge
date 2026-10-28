#include "eth.h"

#include "core/board_config.h"

#if AF_NQ_FAMILY && CONFIG_ETH_SPI_ETHERNET_W5500

#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"

#include "core/state.h"
#include "hw/tmp451.h"
#include "net/wifi.h"

static const char *TAG = "eth";

#define ETH_HOST SPI3_HOST  // SPI2 belongs to an SPI-wired screen
#define PIN_SCLK GPIO_NUM_2
#define PIN_MOSI GPIO_NUM_12
#define PIN_MISO GPIO_NUM_16
#define PIN_CS GPIO_NUM_21
#define W5500_VERSIONR 0x0039
#define W5500_VERSION 0x04

static bool s_up = false;
static char s_ip[16] = "";

// One register read in the W5500 frame format: 16-bit address, control byte, data.
static bool probe()
{
    spi_device_interface_config_t dev = {};
    dev.command_bits = 16;
    dev.address_bits = 8;
    dev.mode = 0;
    dev.clock_speed_hz = 1000000;
    dev.spics_io_num = PIN_CS;
    dev.queue_size = 1;
    spi_device_handle_t h;
    if (spi_bus_add_device(ETH_HOST, &dev, &h) != ESP_OK) return false;
    spi_transaction_t t = {};
    t.cmd = W5500_VERSIONR;
    t.addr = 0x00;  // common register block, read
    t.length = 8;
    t.flags = SPI_TRANS_USE_RXDATA;
    bool ok = spi_device_polling_transmit(h, &t) == ESP_OK && t.rx_data[0] == W5500_VERSION;
    spi_bus_remove_device(h);
    return ok;
}

static void on_event(void *, esp_event_base_t base, int32_t id, void *data)
{
    if (base == ETH_EVENT && id == ETHERNET_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "cable connected");
    } else if (base == ETH_EVENT && id == ETHERNET_EVENT_DISCONNECTED) {
        ESP_LOGW(TAG, "cable disconnected");
        s_up = false;
        s_ip[0] = 0;
        wifi::eth_changed();
    } else if (base == IP_EVENT && id == IP_EVENT_ETH_GOT_IP) {
        auto *ev = (ip_event_got_ip_t *) data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&ev->ip_info.ip));
        s_up = true;
        ESP_LOGW(TAG, "Ethernet up, ip %s", s_ip);
        wifi::eth_changed();
    }
}

namespace eth {

void start()
{
    // on the NerdQX and NerdOctaxe-γ GPIO2 (and 12) select the die-sensor mux
    if (AF_TMP451_MUXES && tmp451::chips() > 0) return;

    spi_bus_config_t bus = {};
    bus.sclk_io_num = PIN_SCLK;
    bus.mosi_io_num = PIN_MOSI;
    bus.miso_io_num = PIN_MISO;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    if (spi_bus_initialize(ETH_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return;
    if (!probe()) {
        spi_bus_free(ETH_HOST);
        gpio_reset_pin(PIN_SCLK);
        gpio_reset_pin(PIN_MOSI);
        gpio_reset_pin(PIN_CS);
        ESP_LOGI(TAG, "no Ethernet adapter");
        return;
    }
    ESP_LOGW(TAG, "W5500 Ethernet adapter found");

    spi_device_interface_config_t dev = {};
    dev.mode = 0;
    dev.clock_speed_hz = 20 * 1000 * 1000;
    dev.spics_io_num = PIN_CS;
    dev.queue_size = 20;
    eth_w5500_config_t w5500 = ETH_W5500_DEFAULT_CONFIG(ETH_HOST, &dev);
    w5500.int_gpio_num = -1;
    w5500.poll_period_ms = 1;  // no interrupt line on the adapter
    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.reset_gpio_num = -1;
    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500, &mac_cfg);
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_cfg);
    esp_eth_config_t cfg = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t h = NULL;
    if (!mac || !phy || esp_eth_driver_install(&cfg, &h) != ESP_OK) {
        ESP_LOGE(TAG, "Ethernet driver failed to start");
        return;
    }
    uint8_t addr[6];
    esp_read_mac(addr, ESP_MAC_ETH);  // the W5500 has no address of its own
    esp_eth_ioctl(h, ETH_CMD_S_MAC_ADDR, addr);

    esp_netif_config_t ncfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *netif = esp_netif_new(&ncfg);
    esp_netif_set_hostname(netif, g_state.hostname);
    esp_netif_attach(netif, esp_eth_new_netif_glue(h));
    esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, on_event, NULL);
    esp_eth_start(h);
}

bool up() { return s_up; }
const char *ip() { return s_ip; }

}

#else
namespace eth {
void start() {}
bool up() { return false; }
const char *ip() { return ""; }
}
#endif
