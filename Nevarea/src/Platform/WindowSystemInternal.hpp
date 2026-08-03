#pragma once

#include "lib/WindowSystem.hpp"

#ifdef NEVAREA_PLATFORM_WINDOWS
	#define WIN32_LEAN_AND_MEAN
	#ifndef NOMINMAX
        #define NOMINMAX
    #endif
	#include <Windows.h>

namespace Nevarea {
	HWND window_get_hwnd(WindowHandle window);
	HINSTANCE window_get_hinstance(WindowHandle window);
}
#endif
