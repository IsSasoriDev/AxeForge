# The board is a data file: boards/<id>.toml, turned into core/board_config.h
# (and board.cmake, for the values this file needs) at configure time. The
# board is picked by its CONFIG_AF_BOARD_* choice from sdkconfig.
# (IDF evaluates this file once before sdkconfig is read; nothing to pick then.)
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    set(AF_BOARDS_DIR ${CMAKE_CURRENT_LIST_DIR}/../boards)
    file(GLOB AF_BOARD_FILES ${AF_BOARDS_DIR}/*.toml ${AF_BOARDS_DIR}/families/*.toml)
    foreach(f ${AF_BOARD_FILES})
        file(STRINGS ${f} kline REGEX "^kconfig *= *\"")
        if(kline)
            string(REGEX REPLACE ".*\"(.*)\".*" "\\1" kname "${kline}")
            if(CONFIG_${kname})
                set(AF_BOARD_FILE ${f})
            endif()
        endif()
    endforeach()
    if(NOT AF_BOARD_FILE)
        message(FATAL_ERROR "No boards/*.toml names the CONFIG_AF_BOARD_* choice set in sdkconfig")
    endif()
    set(AF_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/board)
    idf_build_get_property(python PYTHON)
    execute_process(
        COMMAND ${python} ${CMAKE_CURRENT_LIST_DIR}/../tools/gen_board.py ${AF_BOARD_FILE} ${AF_GEN_DIR}
        RESULT_VARIABLE gen_result)
    if(NOT gen_result EQUAL 0)
        message(FATAL_ERROR "tools/gen_board.py failed for ${AF_BOARD_FILE}")
    endif()
    # a change to a board file or the generator re-runs this configure step
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        ${AF_BOARD_FILES} ${CMAKE_CURRENT_LIST_DIR}/../tools/gen_board.py)
    include(${AF_GEN_DIR}/board.cmake)
    message(STATUS "AxeForge board: ${AF_BOARD_ID} (${AF_BOARD_FILE})")
endif()

file(GLOB_RECURSE SRCS "*.cpp" "*.c")
if(NOT CMAKE_BUILD_EARLY_EXPANSION AND NOT AF_HAS_LCD)
    # no LCD: the OLED module (ui/oled.cpp) stands in for the LVGL screen UI
    list(FILTER SRCS EXCLUDE REGEX ".*/ui/(display|ui|ui_small)\.cpp$")
    list(FILTER SRCS EXCLUDE REGEX ".*/ui/fonts/.*")
endif()
idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "." ${AF_GEN_DIR}
    EMBED_TXTFILES "web/index.html"
    REQUIRES driver esp_wifi esp_netif nvs_flash esp_http_server esp_timer
             esp_lcd json mbedtls lwip esp_app_format app_update esp_http_client esp_https_ota
             tcp_transport esp-tls stratum_v2 esp_adc esp_eth wifi_provisioning bt
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers -Wno-format-truncation
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-enum-enum-conversion>)
