#pragma once

#include "VulkanDevice.hpp"
#include "lib/WindowSystem.hpp"

#include "Core/n_pch.hpp"

namespace Nevarea::Renderer {
	struct DeletionQueue {
		std::vector<std::function<void()>> deletors;
	};

	struct SwapchainData {
        VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
        VkFormat format = VK_FORMAT_B8G8R8A8_SRGB;
        VkColorSpaceKHR color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        uint32_t image_count = 0;
        VkImageUsageFlags image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	};

	struct SwapchainContext {
        VkSwapchainKHR swapchain;
        VkFormat image_format;
        VkExtent2D extent;

        SwapchainData data;

        uint32_t current_image_index;

        std::vector<VkImage> images;
        std::vector<VkImageView> image_views;

        bool resized = false;
	};

    struct FrameContext {
        std::vector<VkSemaphore> image_available;
        std::vector<VkSemaphore> render_finished;

        VkSemaphore timeline;
        uint64_t timeline_value;
        uint64_t frame_timeline_target[MAX_FRAMES_IN_FLIGHT];

        uint32_t current_frame;
        uint64_t present_id = 0;

        VkCommandPool command_pool;
        std::vector<VkCommandBuffer> command_buffers;

        DeletionQueue deletion_queues[MAX_FRAMES_IN_FLIGHT];
    };

    struct SurfaceContext {
        VkSurfaceKHR surface;
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> supported_formats;
        std::vector<VkPresentModeKHR> supported_present_modes;
    };

    NEVAREA_FORCE_INLINE bool has_available_swapchain_support(const SurfaceContext& surface) {
        return !surface.supported_formats.empty() && !surface.supported_present_modes.empty();
    }

    void query_swapchain_support(VkPhysicalDevice physical_device, SurfaceContext& surface);

    void vulkan_swapchain_init(SwapchainContext& swapchain, DeviceContext& device, SurfaceContext& surface, WindowHandle window, VkSwapchainKHR old_swapchain = VK_NULL_HANDLE);
    void recreate_swapchain(SwapchainContext& swapchain, DeviceContext& device, SurfaceContext& surface, WindowHandle window);
    void vulkan_swapchain_destroy(SwapchainContext& swapchain, VkDevice device);

    void vulkan_frame_sync_init(FrameContext& frame_sync, DeviceContext& device_context, uint32_t swapchain_image_count);
    void vulkan_frame_sync_destroy(FrameContext& frame_sync, VkDevice device);

    void vulkan_frame_sync_ensure_present_semaphores(FrameContext& frame_sync, VkDevice device, uint32_t swapchain_image_count);
}
