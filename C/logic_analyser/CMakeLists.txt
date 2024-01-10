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

if (NOT TARGET utilities)
	add_subdirectory(./utilities utilities)
endif()

project(logic_analyser C CXX ASM)

add_library(logic_analyser logic_analyser.c)
target_include_directories(logic_analyser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(logic_analyser hardware_dma hardware_pio pico_multicore pico_stdlib utilities)

add_executable(logic_analyser_main main.c)
target_link_libraries(logic_analyser_main utilities logic_analyser pico_stdlib)

pico_sdk_init()
pico_enable_stdio_usb(logic_analyser_main 0)
pico_enable_stdio_uart(logic_analyser_main 1)
pico_add_extra_outputs(logic_analyser_main)

#compile options
#**************************************************************************************
target_compile_options(logic_analyser_main PUBLIC "-Wall" PUBLIC "-Wextra")

if (${CMAKE_BUILD_TYPE} STREQUAL Debug)
	target_compile_options(logic_analyser_main PUBLIC "-g")
elseif (${CMAKE_BUILD_TYPE} STREQUAL Release)
	target_compile_options(logic_analyser_main PUBLIC "-DNDEBUG" PUBLIC "-O3")
endif()
#**************************************************************************************
