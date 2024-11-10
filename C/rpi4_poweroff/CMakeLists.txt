cmake_minimum_required(VERSION 3.25.1)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

project(rpi4_poweroff LANGUAGES C)

add_executable(rpi4_poweroff main.c)
target_link_options(rpi4_poweroff PUBLIC -static)

set(ENV{PKG_CONFIG_PATH} "./libgpiod/aarch64/lib/pkgconfig")
find_package(PkgConfig REQUIRED)
pkg_check_modules(gpiod REQUIRED IMPORTED_TARGET libgpiod=2.1.1)
target_include_directories(rpi4_poweroff SYSTEM PUBLIC "${gpiod_INCLUDE_DIRS}")
target_link_directories(rpi4_poweroff PUBLIC "${gpiod_LIBRARY_DIRS}")
target_link_libraries(rpi4_poweroff PUBLIC "${gpiod_LIBRARIES}")

#compile options
#***************************************************************************************
target_compile_options(rpi4_poweroff PUBLIC "-Wall" "-Wextra")

if (${CMAKE_BUILD_TYPE} STREQUAL Debug)
	target_compile_options(rpi4_poweroff PUBLIC "-g")
elseif (${CMAKE_BUILD_TYPE} STREQUAL Release)
	target_compile_options(rpi4_poweroff PUBLIC "-DNDEBUG" "-O3" "-flto")
endif()
#***************************************************************************************
