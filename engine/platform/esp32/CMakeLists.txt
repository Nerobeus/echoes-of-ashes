# Firmware Echoes of Ashes pour ESP32-S3 (ESP-IDF 5.2)
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(echoes)

# « idf.py flash » écrit aussi le paquet monde dans sa partition : celui sans les intérieurs (monde_esp32.pak, écrit par
# tools/maj_monde.js ; ils sont sur la carte SD, /sd/echoes/interieurs.bin). Édition enfant (idf_enfant.cmd) : celui de
# engine/data/enfant (maj_monde.js --enfant ; carte SD : /sd/echoes_enfant)
idf_build_get_config(echoes_enfant CONFIG_ECHOES_ENFANT)
if(echoes_enfant)
    esptool_py_flash_to_partition(flash "monde" "${CMAKE_CURRENT_SOURCE_DIR}/../../data/enfant/monde_esp32.pak")
else()
    esptool_py_flash_to_partition(flash "monde" "${CMAKE_CURRENT_SOURCE_DIR}/../../data/monde_esp32.pak")
endif()
