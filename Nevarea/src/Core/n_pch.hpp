#pragma once

#define MAX_FRAMES_IN_FLIGHT 2

#include <iostream>
#include <optional>
#include <set>
#include <cstddef>
#include <stdexcept>
#include <stdint.h>
#include <vector>
#include <algorithm>
#include <fstream>
#include <functional>
#include <limits>
#include <cstring>
#include <cstdint>

namespace Nevarea {
    using u8 = uint8_t;
    using u16 = uint16_t;
    using u32 = uint32_t;
    using u64 = uint64_t;
    using usize = size_t;

    using i8 = int8_t;
    using i16 = int16_t;
    using i32 = int32_t;
    using i64 = int64_t;
    using isize = ptrdiff_t;

    using f32 = float;
    using f64 = double;
}

#include "lib/Core.hpp"
#include "LogInternal.hpp"

#include <volk.h>
#include <vulkan/vk_enum_string_helper.h>
