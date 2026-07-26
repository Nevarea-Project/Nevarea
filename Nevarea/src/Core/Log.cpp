#include "LogInternal.hpp"
#include <cstdarg>
#include <cstdio>

namespace Nevarea {
    LogCallback g_log_callback = nullptr;
    void* g_log_user = nullptr;

    void log_dispatch(LogLevel level, const char *format, ...) {
        if (!g_log_callback) return;

        char buf[1024];
        va_list args;
        va_start(args, format);
        std::vsnprintf(buf, sizeof(buf), format, args);
        va_end(args);

        g_log_callback(level, buf, g_log_user);
    }

    NEVAREA_API void logger_set_callback(LogCallback callback, void* user) {
        g_log_callback = callback;
        g_log_user = user;
    }
}
