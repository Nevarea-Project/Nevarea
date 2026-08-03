#pragma once

#include "Core/n_pch.hpp"

namespace Nevarea::Renderer {
	VKAPI_ATTR VkBool32 VKAPI_CALL debug_messenger_callback([[maybe_unused]] VkDebugUtilsMessageSeverityFlagBitsEXT message_severity, [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT message_type, const VkDebugUtilsMessengerCallbackDataEXT* callback_data, [[maybe_unused]] void* user_data);
	VkResult create_debug_utils_messenger_ext(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* create_info, const VkAllocationCallbacks* allocator, VkDebugUtilsMessengerEXT* debug_messenger);
	void destroy_debug_utils_messenger_ext(VkInstance instance, VkDebugUtilsMessengerEXT debug_messenger, const VkAllocationCallbacks* allocator);
	void populate_debug_create_info(VkDebugUtilsMessengerCreateInfoEXT& debug_create_info);
	void vulkan_set_debug_name(VkDevice device, VkObjectType type, uint64_t handle, const char* name);
}

#ifdef NEVAREA_DEBUG
	#define VK_NAME(device, type, handle, name) \
		vulkan_set_debug_name(device, type, reinterpret_cast<uint64_t>(handle), name)
#else
	#define VK_NAME(device, type, handle, name) ((void)0)
#endif
