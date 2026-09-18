# TimerRelay - top-level project CMake
# ESP-IDF v6.0.2, target esp32c6 (Seeed XIAO ESP32C6 + 6-channel relay board)
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(timer_relay)
