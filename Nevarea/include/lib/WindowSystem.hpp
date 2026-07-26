#pragma once

#include "Core.hpp"

#include <cstdint>

namespace Nevarea {
	enum class WindowHandle : uint32_t { INVALID = 0 };

	struct NvWinExtent {
		uint32_t width;
		uint32_t height;
	};

	NEVAREA_API WindowHandle window_create(void* native_handle);
	NEVAREA_API void window_destroy(WindowHandle window);

	NEVAREA_API NvWinExtent window_get_extent(WindowHandle window);
	NEVAREA_API void window_set_extent(WindowHandle window, NvWinExtent extent);

	NEVAREA_API void window_system_wait_events();
}