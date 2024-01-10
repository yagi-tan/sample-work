cmake_minimum_required(VERSION 3.13)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)

project(utilities VERSION 1.0)

add_library(utilities utilities.c)
target_include_directories(utilities PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

#compile options
#**************************************************************************************
target_compile_options(utilities PUBLIC "-Wall" PUBLIC "-Wextra")

if (${CMAKE_BUILD_TYPE} STREQUAL Debug)
	target_compile_options(utilities PUBLIC "-g")
elseif (${CMAKE_BUILD_TYPE} STREQUAL Release)
	target_compile_options(utilities PUBLIC "-DNDEBUG" PUBLIC "-O3")
endif()
#**************************************************************************************
