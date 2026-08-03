#pragma once

#define NV_VALIDATE(condition, ret, ...) \
    do { if (!(condition)) { NEVAREA_LOG(LogLevel::ERR, __VA_ARGS__); return (ret); } } while (0)

#define NV_CHECK_VK(expr, ...) \
    do { \
        VkResult _vk = (expr); \
        if (_vk < 0) { \
            NEVAREA_LOG(LogLevel::ERR, "Vulkan call failed: %s", string_VkResult(_vk)); \
            return NvResult::VK_FAILURE; \
        } \
    } while(0)
