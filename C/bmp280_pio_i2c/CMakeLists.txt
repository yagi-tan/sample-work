cmake_minimum_required(VERSION 3.13)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(PICO_BOARD "pico")
if (${CMAKE_BUILD_TYPE} STREQUAL Debug)
	set(PICO_DEOPTIMIZED_DEBUG 1)
endif()
set(PICO_PLATFORM "rp2040")
set(PICO_SDK_PATH "./pico-sdk")
include(./pico-sdk/external/pico_sdk_import.cmake)

add_subdirectory(./BMP2-Sensor-API bmp2_api)
add_subdirectory(./logic_analyser logic_analyser)
if (NOT TARGET utilities)
	add_subdirectory(./utilities utilities)
endif()

project(bmp280_pio_i2c C CXX ASM)
add_executable(bmp280_pio_i2c main.c own_i2c.c interrupts.c)

pico_sdk_init()
pico_enable_stdio_usb(bmp280_pio_i2c 0)
pico_enable_stdio_uart(bmp280_pio_i2c 1)
pico_add_extra_outputs(bmp280_pio_i2c)
pico_generate_pio_header(bmp280_pio_i2c ${CMAKE_CURRENT_LIST_DIR}/own_i2c.pio)

#compile options
#***************************************************************************************
target_compile_options(bmp280_pio_i2c PUBLIC "-Wall" PUBLIC "-Wextra")

if (${CMAKE_BUILD_TYPE} STREQUAL Debug)
	target_compile_options(bmp280_pio_i2c PUBLIC "-g" PUBLIC "-DPARAM_ASSERTIONS_ENABLE_ALL")
elseif (${CMAKE_BUILD_TYPE} STREQUAL Release)
	target_compile_options(bmp280_pio_i2c PUBLIC "-DNDEBUG" PUBLIC "-O3")
endif()
#***************************************************************************************

target_link_libraries(bmp280_pio_i2c logic_analyser utilities bmp2_api pico_stdlib hardware_dma hardware_pio)
