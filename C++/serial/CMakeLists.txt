cmake_minimum_required(VERSION 3.0)

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

project(serial)
add_executable(serial main.cpp)

#compile options
#**************************************************************************************
target_compile_options(serial PUBLIC "-Wall" PUBLIC "-Wextra")

if (${CMAKE_BUILD_TYPE} STREQUAL Debug)
	target_compile_options(serial PUBLIC "-g")
elseif (${CMAKE_BUILD_TYPE} STREQUAL Release)
	target_compile_options(serial PUBLIC "-DNDEBUG" PUBLIC "-O3")
endif()
#**************************************************************************************
