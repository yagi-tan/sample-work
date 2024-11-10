#ifndef MAIN_H
#define MAIN_H

#ifdef TARGET_WASM
#include <emscripten/emscripten.h>
#elif !defined(EMSCRIPTEN_KEEPALIVE)
#define EMSCRIPTEN_KEEPALIVE
#endif

#ifndef SPDLOG_COMPILED_LIB
#define SPDLOG_COMPILED_LIB
#endif
#include <spdlog/spdlog.h>

#endif
