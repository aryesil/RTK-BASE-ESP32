cmake_minimum_required(VERSION 3.16.0)

# ESP Insights and RainMaker arrive through the Arduino component's optional
# dependencies. Nothing here uses them, and esp_insights embeds a certificate
# file this project does not ship, which fails the build outright.
set(EXCLUDE_COMPONENTS
    espressif__esp_insights
    espressif__esp_rainmaker
    espressif__esp_diagnostics
    espressif__esp_diag_data_store
    espressif__rmaker_common
    espressif__cbor
    espressif__json_parser
    espressif__json_generator)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(rtk-base-esp32)
