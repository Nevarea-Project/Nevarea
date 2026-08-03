#include "VulkanDebug.hpp"
#include "Core/n_pch.hpp"

namespace Nevarea::Renderer {
	VKAPI_ATTR VkBool32 VKAPI_CALL debug_messenger_callback(
		VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
		[[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT message_type,
		const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
		[[maybe_unused]] void* user_data
	) {
	    LogLevel level = LogLevel::TRACE;
		if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) level = LogLevel::ERR;
		else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) level = LogLevel::WARN;
		else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) level = LogLevel::INFO;

	    NEVAREA_LOG(level, "validation layer: %s", callback_data ? callback_data->pMessage : "null");

		return VK_FALSE;
	}

	VkResult create_debug_utils_messenger_ext(
		VkInstance instance,
		const VkDebugUtilsMessengerCreateInfoEXT* create_info,
		const VkAllocationCallbacks* allocator,
		VkDebugUtilsMessengerEXT* debug_messenger
	) {
	    return vkCreateDebugUtilsMessengerEXT(instance, create_info, allocator, debug_messenger);
	}

	void destroy_debug_utils_messenger_ext(
		VkInstance instance,
		VkDebugUtilsMessengerEXT debug_messenger,
		const VkAllocationCallbacks* allocator
	) {
        vkDestroyDebugUtilsMessengerEXT(instance, debug_messenger, allocator);
	}

	void populate_debug_create_info(VkDebugUtilsMessengerCreateInfoEXT& debug_create_info) {
		debug_create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		debug_create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		debug_create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		debug_create_info.pfnUserCallback = debug_messenger_callback;
		debug_create_info.pUserData = nullptr;
	}

	void vulkan_set_debug_name(VkDevice device, VkObjectType type, uint64_t handle, const char* name) {
		VkDebugUtilsObjectNameInfoEXT info{};
		info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
		info.objectType = type;
		info.objectHandle = handle;
		info.pObjectName = name;

		vkSetDebugUtilsObjectNameEXT(device, &info);
	}
}
