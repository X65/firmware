cmake_minimum_required(VERSION 3.24)

set(CMAKE_C_STANDARD 23)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/src/pico-sdk)
set(PICO_EXTRAS_PATH ${CMAKE_CURRENT_LIST_DIR}/src/pico-extras)

set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

project(RP65816 C CXX ASM)

pico_sdk_init()

add_subdirectory(src)
