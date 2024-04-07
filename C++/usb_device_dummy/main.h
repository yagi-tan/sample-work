#ifndef MAIN_H
#define MAIN_H

#ifndef SPDLOG_COMPILED_LIB
#define SPDLOG_COMPILED_LIB
#endif
#include <spdlog/spdlog.h>

bool init_sys();
void exit_sys();

#endif
