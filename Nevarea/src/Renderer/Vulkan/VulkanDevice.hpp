#pragma once

#include "Core/n_pch.hpp"
#include "lib/Rendering.hpp"

namespace Nevarea::Renderer {
    struct DeviceCapabilities {
	    bool memory_priority = false;
		bool pageable_memory = false;
		bool present_id = false;
		bool present_wait = false;
		bool present_id2 = false;
		bool present_wait2 = false;
		bool swapchain_maintenance1 = false;
		bool descriptor_heap = false;
		bool mutable_descriptor_type = false;
		bool shader_object = false;
		bool extended_dynamic_state3 = false;
		bool calibrated_timesteps = false;
		bool shader_module_identifier = false;
	};

	struct DeviceContext {
		VkPhysicalDevice physical_device;
		VkDevice device;

		VkQueue graphics_queue;
		VkQueue present_queue;
		VkQueue compute_queue;
		VkQueue transfer_queue;

		DeviceCapabilities capabilities;

		bool device_lost = false;

		u32 graphics_family_index;
		u32 compute_family_index;
		u32 transfer_family_index;
		u32 present_family_index;

		std::vector<const char*> enabled_extensions;
		std::vector<const char*> requested_extensions;

		const void* user_feature_chain = nullptr;
	};

	struct QueueFamilyIndices {
		std::optional<u32> graphics_family;
		std::optional<u32> present_family;
		std::optional<u32> compute_family;
		std::optional<u32> transfer_family;

		bool is_complete() const {
			return graphics_family.has_value() && present_family.has_value() && compute_family.has_value() && transfer_family.has_value();
		}
	};

	QueueFamilyIndices find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface);

	void vulkan_device_init(DeviceContext& device_context, VkInstance instance, VkSurfaceKHR surface);
	void vulkan_device_destroy(DeviceContext* device_context);

	void vulkan_device_pick_physical_device(VkInstance instance, VkSurfaceKHR surface, DeviceContext* device_context);
	void vulkan_device_create_logical_device(VkSurfaceKHR surface, DeviceContext* device_context);
}
