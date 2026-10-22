file(GLOB_RECURSE SRCS "*.cpp" "*.c")
idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "."
    EMBED_TXTFILES "web/index.html"
    REQUIRES driver esp_wifi esp_netif nvs_flash esp_http_server esp_timer
             esp_lcd json mbedtls lwip esp_app_format app_update esp_http_client esp_https_ota
             tcp_transport esp-tls stratum_v2
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers -Wno-format-truncation
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-enum-enum-conversion>)
