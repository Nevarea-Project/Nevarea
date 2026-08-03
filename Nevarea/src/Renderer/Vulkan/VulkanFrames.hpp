#pragma once

#include "VulkanSwapchain.hpp"
#include "VulkanResourceManager.hpp"
#include "VulkanContext.hpp"

namespace Nevarea::Renderer {
	void transition_image(VkCommandBuffer cmd, VkImage image,
		VkImageLayout old_layout, VkImageLayout new_layout,
		VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
		VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access,
        VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);

	void end_frame_present(FrameContext& frame, SwapchainContext& swapchain, DeviceContext& device, SurfaceContext& surface, WindowHandle window, VkCommandBuffer cmd, VkSemaphore transfer_timeline, uint64_t transfer_wait_value);

	VkCommandBuffer begin_frame(FrameContext& frame, SwapchainContext& swapchain, DeviceContext& device, SurfaceContext& surface, WindowHandle window);
	void begin_rendering(VkCommandBuffer cmd, const PassData& pass, SwapchainContext& swapchain, ResourceManager& resources);
	void end_frame_rendering(FrameContext& frame, SwapchainContext& swapchain, DeviceContext& device, SurfaceContext& surface, WindowHandle window, VkCommandBuffer cmd, bool any_present, VkSemaphore transfer_timeline, uint64_t transfer_wait_value);

	bool vulkan_wait_for_present(DeviceContext& device, SwapchainContext& swapchain, uint64_t present_id, uint64_t timeout_ns);
}
