#include "Platform/WindowSystemInternal.hpp"

#include "Core/n_pch.hpp"

namespace Nevarea {
	namespace {
		struct WindowState {
			u32 width;
			u32 height;
			bool is_active;

			#ifdef NEVAREA_PLATFORM_WINDOWS
				HWND hwnd;
				HINSTANCE hinstance;
			#endif
		};

		constexpr u32 MAX_WINDOWS = 16;
		WindowState g_windows[MAX_WINDOWS];

		WindowState* resolve(WindowHandle window) {
			u32 value = static_cast<u32>(window);

			NEVAREA_ASSERT(value != 0 && value <= MAX_WINDOWS, "WINDOW SYSTEM", "Invalid WindowHandle!");
			WindowState* window_state = &g_windows[value - 1];

			NEVAREA_ASSERT(window_state->is_active, "WINDOW SYSTEM", "WindowHandle refers to a destroyed window!");
			return window_state;
		}
	}

	WindowHandle window_create(void* native_handle) {
		for (u32 i = 0; i < MAX_WINDOWS; ++i) {
			if (g_windows[i].is_active) continue;

			WindowState& window_state = g_windows[i];
			window_state = {};
			window_state.is_active = true;

			#ifdef NEVAREA_PLATFORM_WINDOWS
				window_state.hwnd = (HWND)native_handle;
				window_state.hinstance = (HINSTANCE)GetWindowLongPtr(window_state.hwnd, GWLP_HINSTANCE);

				RECT rect;
				if (GetClientRect(window_state.hwnd, &rect)) {
					window_state.width = rect.right - rect.left;
					window_state.height = rect.bottom - rect.top;
				}
			#endif

			return static_cast<WindowHandle>(i + 1);
		}

		NEVAREA_ASSERT(false, "WINDOW SYSTEM", "MAX_WINDOWS exceeded!");
		return WindowHandle::INVALID;
	}

	void window_destroy(WindowHandle window) {
		WindowState* window_state = resolve(window);
		*window_state = {};
	}

	NvWinExtent window_get_extent(WindowHandle window) {
		WindowState* window_state = resolve(window);
		return { window_state->width, window_state->height };
	}

	void window_set_extent(WindowHandle window, NvWinExtent extent) {
		WindowState* window_state = resolve(window);
		window_state->width = extent.width;
		window_state->height = extent.height;
	}

	#ifdef NEVAREA_PLATFORM_WINDOWS
	HWND window_get_hwnd(WindowHandle window) {
		return resolve(window)->hwnd;
	}

	HINSTANCE window_get_hinstance(WindowHandle window) {
		return resolve(window)->hinstance;
	}
	#endif

	void window_system_wait_events() {
		#ifdef NEVAREA_PLATFORM_WINDOWS
			MSG msg;
			if (GetMessage(&msg, NULL, 0, 0)) {
				while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
					TranslateMessage(&msg);
					DispatchMessage(&msg);
				}
			}
		#endif
	}
}
