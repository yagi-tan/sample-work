cmake_minimum_required(VERSION 3.0)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_TOOLCHAIN_FILE
	"../wasm/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")

project(usb_web_backend C CXX)

add_executable(channelData wasm_cpp/main.cpp)
add_custom_command(TARGET channelData POST_BUILD
	COMMAND "ln" ARGS "-rs" "channelData.*" "../wasm_cpp/."
	COMMENT "Make shortcut to build artifacts in 'wasm_cpp' folder.")

#spdlog library
#***************************************************************************************
set(spdlog_DIR "../wasm/spdlog/build/wasm/lib/cmake/spdlog")
find_package(spdlog REQUIRED)

if (${CMAKE_BUILD_TYPE} STREQUAL Debug)
	target_compile_options(channelData PUBLIC "-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE")
elseif (${CMAKE_BUILD_TYPE} STREQUAL Release)
	target_compile_options(channelData PUBLIC "-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO")
endif()
target_link_libraries(channelData spdlog::spdlog)
#***************************************************************************************

if (NOT TARGET usb_data_tools)
	set(TARGET_WASM ON CACHE BOOL "")
	add_subdirectory(../usb_data_tools usb_data_tools)
endif()
target_link_libraries(channelData usb_data_tools)

#compile options
#***************************************************************************************
if (${CMAKE_BUILD_TYPE} STREQUAL Debug)
	list(APPEND compile_opts "-g")
elseif (${CMAKE_BUILD_TYPE} STREQUAL Release)
	list(APPEND compile_opts "-DNDEBUG" "-O3" "-flto")
endif()

list(APPEND compile_opts "-Wall" "-Wextra" "-fno-exceptions" "-fno-rtti")
list(APPEND emscripten_all_opts "-sSTRICT" "--no-entry")
list(APPEND emscripten_link_opts "-sEXPORTED_RUNTIME_METHODS=[ccall,HEAPU8]" "-sMODULARIZE" "-sEXPORT_ES6"
	"-sENVIRONMENT=node" "-sFILESYSTEM=0" "-sALLOW_MEMORY_GROWTH" "-sEXPORTED_FUNCTIONS=[_malloc,_free]"
	"-sWASM_BIGINT" "-sINCOMING_MODULE_JS_API=[wasm,wasmMemory,instantiateWasm]")

target_compile_options(channelData PUBLIC ${compile_opts} ${emscripten_all_opts})
target_link_options(channelData PUBLIC ${emscripten_all_opts} ${emscripten_link_opts})
#***************************************************************************************
