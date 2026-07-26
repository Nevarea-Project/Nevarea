#include "VulkanSwapchain.hpp"
#include "VulkanDebug.hpp"
#include "lib/Core.hpp"

namespace Nevarea::Renderer {
	void query_swapchain_support(VkPhysicalDevice physical_device, SurfaceContext& surface) {
	    VK_ASSERT(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface.surface, &surface.capabilities));

		uint32_t format_count = 0;
		VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface.surface, &format_count, nullptr));
		if (format_count > 0) {
			surface.supported_formats.resize(format_count);
			VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface.surface, &format_count, surface.supported_formats.data()));
		}

		uint32_t present_mode_count = 0;
		VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface.surface, &present_mode_count, nullptr));
		if (present_mode_count > 0) {
			surface.supported_present_modes.resize(present_mode_count);
			VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface.surface, &present_mode_count, surface.supported_present_modes.data()));
		}
	}

	static VkSurfaceFormatKHR choose_surface_format(std::vector<VkSurfaceFormatKHR> surface_formats, VkFormat desired_format, VkColorSpaceKHR desired_color_space) {
		for (const auto& format : surface_formats) {
			if (format.format == desired_format && format.colorSpace == desired_color_space)
				return format;
		}

		NEVAREA_ASSERT(false, "VULKAN SWAPCHAIN", "Could not find a compatible surface format!");
		return surface_formats[0]; // :(
	}

	static VkExtent2D choose_swapchain_extent(const VkSurfaceCapabilitiesKHR& capabilities, VkExtent2D initial_window_extent) {
		if (capabilities.currentExtent.width != (std::numeric_limits<uint32_t>::max)())
			return capabilities.currentExtent;
		else {
			VkExtent2D extent = initial_window_extent;

			extent.width = (std::max)(
				capabilities.minImageExtent.width,
				(std::min)(capabilities.maxImageExtent.width, extent.width));

			extent.height = (std::max)(
				capabilities.minImageExtent.height,
				(std::min)(capabilities.maxImageExtent.height, extent.height));

			return extent;
		}

		return { 0, 0 };
	}

	static VkPresentModeKHR choose_swapchain_present_mode(const std::vector<VkPresentModeKHR>& present_modes, VkPresentModeKHR desired) {
		for (const auto& present_mode : present_modes)
			if (present_mode == desired) return present_mode;

		return VK_PRESENT_MODE_FIFO_KHR;
	}

	static VkExtent2D query_swapchain_extent(WindowHandle window, DeviceContext& device, SurfaceContext& surface) {
        query_swapchain_support(device.physical_device, surface);
        NvWinExtent win = window_get_extent(window);
        return choose_swapchain_extent(surface.capabilities, { win.width, win.height });
    }

	static void vulkan_swapchain_image_views(SwapchainContext& swapchain, VkDevice device) {
		swapchain.image_views.resize(swapchain.images.size());
		for (size_t i = 0; i < swapchain.images.size(); i++) {
			VkImageViewCreateInfo view_info{};
			view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			view_info.image = swapchain.images[i];
			view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
			view_info.format = swapchain.image_format;
			view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			view_info.subresourceRange.baseMipLevel = 0;
			view_info.subresourceRange.levelCount = 1;
			view_info.subresourceRange.baseArrayLayer = 0;
			view_info.subresourceRange.layerCount = 1;

			VK_ASSERT(vkCreateImageView(device, &view_info, nullptr, &swapchain.image_views[i]));

			char name[64];
			std::snprintf(name, sizeof(name), "swapchain_image_view[%zu]", i);
			VK_NAME(device, VK_OBJECT_TYPE_IMAGE_VIEW, swapchain.image_views[i], name);
		}
	}

	void vulkan_swapchain_init(SwapchainContext& swapchain, DeviceContext& device, SurfaceContext& surface, WindowHandle window, VkSwapchainKHR old_swapchain)
	{
		VkExtent2D swapchain_extent = query_swapchain_extent(window, device, surface);
		VkSurfaceFormatKHR surface_format = choose_surface_format(surface.supported_formats, swapchain.data.format, swapchain.data.color_space);
		VkPresentModeKHR swapchain_present_mode = choose_swapchain_present_mode(surface.supported_present_modes, swapchain.data.present_mode);

		VkSwapchainCreateInfoKHR create_info{};
		create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		create_info.pNext = nullptr;
		create_info.flags = 0;
		create_info.surface = surface.surface;

		create_info.minImageCount = swapchain.data.image_count;
		create_info.imageFormat = surface_format.format;
		create_info.imageColorSpace = surface_format.colorSpace;
		create_info.imageExtent = swapchain_extent;
		create_info.imageArrayLayers = 1;
		create_info.imageUsage = swapchain.data.image_usage;

		uint32_t sharing_families[] = { device.graphics_family_index, device.present_family_index };
		if (device.graphics_family_index != device.present_family_index) {
			create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			create_info.queueFamilyIndexCount = 2;
			create_info.pQueueFamilyIndices = sharing_families;
		} else {
			create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		}

		create_info.preTransform = surface.capabilities.currentTransform;
		create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		create_info.presentMode = swapchain_present_mode;
		create_info.clipped = VK_TRUE;
		create_info.oldSwapchain = old_swapchain;

		VK_ASSERT(vkCreateSwapchainKHR(device.device, &create_info, nullptr, &swapchain.swapchain));
		VK_NAME(device.device, VK_OBJECT_TYPE_SWAPCHAIN_KHR, swapchain.swapchain, "swapchain");

		if (old_swapchain != VK_NULL_HANDLE)
			vkDestroySwapchainKHR(device.device, old_swapchain, nullptr);

		swapchain.image_format = surface_format.format;
		swapchain.extent = swapchain_extent;

		uint32_t swapchain_image_count = 0;
		VK_CHECK(vkGetSwapchainImagesKHR(device.device, swapchain.swapchain, &swapchain_image_count, nullptr));
		swapchain.images.resize(swapchain_image_count);
		VK_CHECK(vkGetSwapchainImagesKHR(device.device, swapchain.swapchain, &swapchain_image_count, swapchain.images.data()));

		vulkan_swapchain_image_views(swapchain, device.device);
	}

	void recreate_swapchain(SwapchainContext& swapchain, DeviceContext& device, SurfaceContext& surface, WindowHandle window) {
	    query_swapchain_support(device.physical_device, surface);
		if (surface.capabilities.currentExtent.width == 0 || surface.capabilities.currentExtent.height == 0)
            return;

	    vkDeviceWaitIdle(device.device);

		for (auto imageView : swapchain.image_views)
			vkDestroyImageView(device.device, imageView, nullptr);

		swapchain.image_views.clear();

		VkFormat old_format = swapchain.image_format;
		VkSwapchainKHR old_handle = swapchain.swapchain;
		vulkan_swapchain_init(swapchain, device, surface, window, old_handle);

		if (swapchain.image_format != old_format)
			std::cerr << "[NEVAREA]: swapchain surface format changed on recreate - graphics pipelines built for the old format are now invalid and must be recreated." << std::endl;

		swapchain.resized = true;
	}

	void vulkan_swapchain_destroy(SwapchainContext& swapchain, VkDevice device) {
		for (size_t i = 0; i < swapchain.image_views.size(); i++)
			vkDestroyImageView(device, swapchain.image_views[i], nullptr);

		vkDestroySwapchainKHR(device, swapchain.swapchain, nullptr);
	}

	void vulkan_frame_sync_init(FrameContext& frame_sync, DeviceContext& device_context, uint32_t swapchain_image_count)
	{
		VkDevice device = device_context.device;

		frame_sync.current_frame = 0;

		frame_sync.image_available.resize(MAX_FRAMES_IN_FLIGHT);

		VkSemaphoreCreateInfo semaphore_info{};
		semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
			VK_ASSERT(vkCreateSemaphore(device, &semaphore_info, nullptr, &frame_sync.image_available[i]));

			char name[64];
			std::snprintf(name, sizeof(name), "image_available[%zu]", i);
			VK_NAME(device, VK_OBJECT_TYPE_SEMAPHORE, frame_sync.image_available[i], name);
		}

		frame_sync.render_finished.resize(swapchain_image_count);
		for (size_t i = 0; i < swapchain_image_count; i++) {
			VK_ASSERT(vkCreateSemaphore(device, &semaphore_info, nullptr, &frame_sync.render_finished[i]));

			char name[64];
			std::snprintf(name, sizeof(name), "render_finished[%zu]", i);
			VK_NAME(device, VK_OBJECT_TYPE_SEMAPHORE, frame_sync.render_finished[i], name);
		}

		VkSemaphoreTypeCreateInfo timeline_type{};
		timeline_type.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
		timeline_type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
		timeline_type.initialValue = 0;

		VkSemaphoreCreateInfo timeline_info{};
		timeline_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		timeline_info.pNext = &timeline_type;

		VK_ASSERT(vkCreateSemaphore(device, &timeline_info, nullptr, &frame_sync.timeline));
		VK_NAME(device, VK_OBJECT_TYPE_SEMAPHORE, frame_sync.timeline, "frame_timeline");

		frame_sync.timeline_value = 0;
		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
			frame_sync.frame_timeline_target[i] = 0;

		//QueueFamilyIndices indices = find_queue_families(physical_device, surface);

		//NEVAREA_ASSERT(indices.graphics_family.has_value(), "VULKAN SWAPCHAIN", "No graphcis queue family found!");

		VkCommandPoolCreateInfo pool_info{};
		pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		pool_info.queueFamilyIndex = device_context.graphics_family_index;

		VK_ASSERT(vkCreateCommandPool(device, &pool_info, nullptr, &frame_sync.command_pool));
		VK_NAME(device, VK_OBJECT_TYPE_COMMAND_POOL, frame_sync.command_pool, "frame_command_pool");

		frame_sync.command_buffers.resize(MAX_FRAMES_IN_FLIGHT);

		VkCommandBufferAllocateInfo allocation_info{};
		allocation_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocation_info.commandPool = frame_sync.command_pool;
		allocation_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocation_info.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

		VK_ASSERT(vkAllocateCommandBuffers(device, &allocation_info, frame_sync.command_buffers.data()));

		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
			char name[64];
			std::snprintf(name, sizeof(name), "command_buffer[%zu]", i);
			VK_NAME(device, VK_OBJECT_TYPE_COMMAND_BUFFER, frame_sync.command_buffers[i], name);
		}
	}

	void vulkan_frame_sync_destroy(FrameContext& frame_sync, VkDevice device)
	{
		for (size_t i = 0; i < frame_sync.image_available.size(); i++)
			vkDestroySemaphore(device, frame_sync.image_available[i], nullptr);

		for (size_t i = 0; i < frame_sync.render_finished.size(); i++)
			vkDestroySemaphore(device, frame_sync.render_finished[i], nullptr);

		vkDestroySemaphore(device, frame_sync.timeline, nullptr);

		if (frame_sync.command_pool != VK_NULL_HANDLE) {
			vkDestroyCommandPool(device, frame_sync.command_pool, nullptr);
			frame_sync.command_pool = VK_NULL_HANDLE;
		}
	}

	void vulkan_frame_sync_ensure_present_semaphores(FrameContext& frame_sync, VkDevice device, uint32_t swapchain_image_count) {
		size_t old_count = frame_sync.render_finished.size();
		if (swapchain_image_count <= old_count) return;

		VkSemaphoreCreateInfo semaphore_info{};
		semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		frame_sync.render_finished.resize(swapchain_image_count);
		for (size_t i = old_count; i < swapchain_image_count; i++) {
			VK_ASSERT(vkCreateSemaphore(device, &semaphore_info, nullptr, &frame_sync.render_finished[i]));

			char name[64];
			std::snprintf(name, sizeof(name), "render_finished[%zu]", i);
			VK_NAME(device, VK_OBJECT_TYPE_SEMAPHORE, frame_sync.render_finished[i], name);
		}
	}
}
