#pragma once

#define MAX_FRAMES_IN_FLIGHT 2

#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>
#include <stdint.h>
#include <vector>
#include <algorithm>
#include <fstream>
#include <functional>
#include <limits>
#include <cstring>

#include "lib/Core.hpp"
#include "LogInternal.hpp"

#ifdef NEVAREA_PLATFORM_WINDOWS
	#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <volk.h>
#include <vulkan/vk_enum_string_helper.h>
