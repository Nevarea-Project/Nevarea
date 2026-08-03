#pragma once

namespace Nevarea {
    enum class LogLevel {
        TRACE,
        INFO,
        WARN,
        ERR // would love to use "ERROR" but c++ is god awful
    };

    using LogCallback = void(*)(LogLevel, const char* msg, void* user);
    NEVAREA_API void logger_set_callback(LogCallback callback, void* user);
}
