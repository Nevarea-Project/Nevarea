#pragma once

#include "lib/Logging.hpp"

namespace Nevarea {
    extern LogCallback g_log_callback;
    extern void* g_log_user;

    void log_dispatch(LogLevel level, const char* format, ...);
}

#define NEVAREA_LOG(level, ...) ::Nevarea::log_dispatch(level, __VA_ARGS__)
