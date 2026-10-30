file(GLOB_RECURSE SRCS "*.cpp" "*.c")
if(CONFIG_AF_BOARD_SUPRAHEX OR CONFIG_AF_BOARD_ULTRAHEX OR CONFIG_AF_BOARD_BITAXE_SUPRA OR CONFIG_AF_BOARD_BITAXE_GAMMA
   OR CONFIG_AF_BOARD_BITAXE_ULTRA OR CONFIG_AF_BOARD_BITAXE_MAX OR CONFIG_AF_BOARD_GAMMA_DUO OR CONFIG_AF_BOARD_BITAXE_GT)
    # no LCD: the OLED module (ui/oled.cpp) stands in for the LVGL screen UI
    list(FILTER SRCS EXCLUDE REGEX ".*/ui/(display|ui|ui_small)\.cpp$")
    list(FILTER SRCS EXCLUDE REGEX ".*/ui/fonts/.*")
endif()
idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "."
    EMBED_TXTFILES "web/index.html"
    REQUIRES driver esp_wifi esp_netif nvs_flash esp_http_server esp_timer
             esp_lcd json mbedtls lwip esp_app_format app_update esp_http_client esp_https_ota
             tcp_transport esp-tls stratum_v2 esp_adc esp_eth wifi_provisioning bt
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers -Wno-format-truncation
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-enum-enum-conversion>)
