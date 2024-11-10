cmake_minimum_required(VERSION 3.0)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

project(usb_device_dummy C CXX)

add_executable(usb_device_dummy main.cpp proc.cpp generator.cpp)
target_link_options(usb_device_dummy PUBLIC -static)

#spdlog library
#***************************************************************************************
find_package(spdlog REQUIRED)

target_compile_options(usb_device_dummy PUBLIC "-DSPDLOG_DISABLE_DEFAULT_LOGGER")
if (${CMAKE_BUILD_TYPE} STREQUAL Debug)
	target_compile_options(usb_device_dummy PUBLIC "-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE")
elseif (${CMAKE_BUILD_TYPE} STREQUAL Release)
	target_compile_options(usb_device_dummy PUBLIC "-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO")
endif()
target_link_libraries(usb_device_dummy spdlog::spdlog)
#***************************************************************************************

#compile options
#***************************************************************************************
target_compile_options(usb_device_dummy PUBLIC "-Wall" "-Wextra")

if (${CMAKE_BUILD_TYPE} STREQUAL Debug)
	target_compile_options(usb_device_dummy PUBLIC "-g")
elseif (${CMAKE_BUILD_TYPE} STREQUAL Release)
	target_compile_options(usb_device_dummy PUBLIC "-DNDEBUG" "-O3" "-flto")
endif()
#***************************************************************************************
