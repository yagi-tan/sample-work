cmake_minimum_required(VERSION 3.0)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

option(TARGET_WASM "Set to ON if project is compiled for WebAssembly." OFF)

if (${TARGET_WASM})
	set(CMAKE_TOOLCHAIN_FILE
		"../wasm/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")
	add_compile_definitions(TARGET_WASM)
endif()

project(usb_data_tools C CXX)

add_library(usb_data_tools generator.cpp interpreter.cpp main.cpp)
target_include_directories(usb_data_tools PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

#spdlog library
#***************************************************************************************
if (${TARGET_WASM})
	set(spdlog_DIR "../wasm/spdlog/build/wasm/lib/cmake/spdlog")
endif()
find_package(spdlog REQUIRED)

if (${CMAKE_BUILD_TYPE} STREQUAL Debug)
	target_compile_options(usb_data_tools PUBLIC "-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE")
elseif (${CMAKE_BUILD_TYPE} STREQUAL Release)
	target_compile_options(usb_data_tools PUBLIC "-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO")
endif()
target_link_libraries(usb_data_tools spdlog::spdlog)
#***************************************************************************************

#compile options
#***************************************************************************************
list(APPEND compile_opts "-Wall" "-Wextra")
if (${CMAKE_BUILD_TYPE} STREQUAL Debug)
	list(APPEND compile_opts "-g")
elseif (${CMAKE_BUILD_TYPE} STREQUAL Release)
	list(APPEND compile_opts "-DNDEBUG" "-O3" "-flto")
endif()

if (${TARGET_WASM})
	list(APPEND compile_opts "-fno-exceptions" "-fno-rtti")
	
	list(APPEND emscripten_all_opts "-sSTRICT" "--no-entry")
	list(APPEND emscripten_link_opts "-sEXPORTED_RUNTIME_METHODS=[ccall,HEAPU8]" "-sMODULARIZE" "-sEXPORT_ES6"
		"-sENVIRONMENT=node" "-sFILESYSTEM=0" "-sALLOW_MEMORY_GROWTH" "-sEXPORTED_FUNCTIONS=[_malloc,_free]"
		"-sWASM_BIGINT" "-sINCOMING_MODULE_JS_API=[wasm,wasmMemory,instantiateWasm]")
endif()

target_compile_options(usb_data_tools PUBLIC ${compile_opts} ${emscripten_all_opts})
target_link_options(usb_data_tools PUBLIC ${emscripten_all_opts} ${emscripten_link_opts})
#***************************************************************************************
