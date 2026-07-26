#include "VulkanFrames.hpp"
#include "Core/n_pch.hpp"
#include "lib/Rendering.hpp"
#include "VulkanTranslate.hpp"

namespace Nevarea::Renderer {
	static NEVAREA_FORCE_INLINE VkCommandBuffer prepare_command_buffer(FrameContext& frame, SwapchainContext& swapchain, DeviceContext& device, SurfaceContext& surface, WindowHandle window) {
		VkResult result = vkAcquireNextImageKHR(device.device, swapchain.swapchain, UINT64_MAX,
			frame.image_available[frame.current_frame], VK_NULL_HANDLE, &swapchain.current_image_index);

		if (result == VK_ERROR_DEVICE_LOST) { device.device_lost = true; return VK_NULL_HANDLE; }

		if (result == VK_ERROR_OUT_OF_DATE_KHR) {
			recreate_swapchain(swapchain, device, surface, window);
			vulkan_frame_sync_ensure_present_semaphores(frame, device.device, static_cast<uint32_t>(swapchain.images.size()));
			return VK_NULL_HANDLE;
		}

		VkCommandBuffer cmd = frame.command_buffers[frame.current_frame];
		NEVAREA_ASSERT(cmd != VK_NULL_HANDLE, "VULKAN FRAMES", "The Drawing Command Buffer is Null!");

		VK_ASSERT(vkResetCommandBuffer(cmd, 0));

		return cmd;
	}

	VkCommandBuffer begin_frame(FrameContext& frame, SwapchainContext& swapchain, DeviceContext& device, SurfaceContext& surface, WindowHandle window) {
		uint64_t wait_value = frame.frame_timeline_target[frame.current_frame];

		VkSemaphoreWaitInfo wait_info{};
		wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
		wait_info.semaphoreCount = 1;
		wait_info.pSemaphores = &frame.timeline;
		wait_info.pValues = &wait_value;

		VkResult wait_result = vkWaitSemaphores(device.device, &wait_info, UINT64_MAX);
		if (wait_result == VK_ERROR_DEVICE_LOST) { device.device_lost = true; return VK_NULL_HANDLE; }
		VK_ASSERT(wait_result);

		vulkan_resources_flush_deletors(frame.deletion_queues[frame.current_frame]);

		VkCommandBuffer cmd = prepare_command_buffer(frame, swapchain, device, surface, window);
		if (cmd == VK_NULL_HANDLE) return VK_NULL_HANDLE;

		VkCommandBufferBeginInfo begin_info{};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		VK_ASSERT(vkBeginCommandBuffer(cmd, &begin_info));

		return cmd;
	}

	void begin_rendering(VkCommandBuffer cmd, const PassData& pass, SwapchainContext& swapchain, ResourceManager& resources) {
		VkRenderingAttachmentInfo color_attachments[NEVAREA_MAX_COLOR_ATTACHMENTS]{};

		NEVAREA_ASSERT(pass.color.size() <= NEVAREA_MAX_COLOR_ATTACHMENTS, "VULKAN FRAMES",
		    "the maximum amount of color attachments has been exceeded!");

		for (size_t i = 0; i < (uint32_t)pass.color.size(); i++) {
		    const ColorAttachment& att = pass.color[i];

            VkRenderingAttachmentInfo color_attachment{};
      		color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            color_attachment.imageView = (pass.present && i == 0) ? swapchain.image_views[swapchain.current_image_index] : vulkan_get_image(resources, { att.image.id, att.image.generation }).view;
            color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            color_attachment.loadOp = to_vk_load_op(att.load);
            color_attachment.storeOp = to_vk_store_op(att.store);
            color_attachment.clearValue = { { { att.clear[0], att.clear[1], att.clear[2], att.clear[3] } } };

            color_attachments[i] = color_attachment;
		}

		VkRenderingInfo rendering_info{};
		rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		rendering_info.renderArea = { {0, 0}, swapchain.extent };
		rendering_info.layerCount = 1;
		rendering_info.colorAttachmentCount = (uint32_t)pass.color.size();
		rendering_info.pColorAttachments = color_attachments;

		if (pass.depth.image.is_valid()) {
            VkRenderingAttachmentInfo depth_attachment{};
            depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depth_attachment.imageView = vulkan_get_image(resources, { pass.depth.image.id, pass.depth.image.generation }).view;
            depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depth_attachment.loadOp = to_vk_load_op(pass.depth.load);
            depth_attachment.storeOp = to_vk_store_op(pass.depth.store);
            depth_attachment.clearValue.depthStencil = { pass.depth.clear, 0 };

            rendering_info.pDepthAttachment = &depth_attachment;
		}

		vkCmdBeginRendering(cmd, &rendering_info);
	}

	static NEVAREA_FORCE_INLINE void handle_queues(FrameContext& frame, SwapchainContext& swapchain, DeviceContext& device, SurfaceContext& surface, WindowHandle window, VkCommandBuffer cmd, VkPipelineStageFlags2 image_stage, VkSemaphore transfer_timeline, uint64_t transfer_wait_value) {
		uint32_t img_idx = swapchain.current_image_index;
		VkSemaphore signal_semaphores[] = { frame.render_finished[img_idx] };

		VkSemaphoreSubmitInfo waits[2]{};
		waits[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
		waits[0].semaphore = frame.image_available[frame.current_frame];
		waits[0].stageMask = image_stage;

		waits[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
		waits[1].semaphore = transfer_timeline;
		waits[1].value = transfer_wait_value;
		waits[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

		uint64_t signal_value = ++frame.timeline_value;

		VkSemaphoreSubmitInfo signal_infos[2]{};
		signal_infos[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
		signal_infos[0].semaphore = frame.render_finished[img_idx];
		signal_infos[0].stageMask = image_stage;

		signal_infos[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
		signal_infos[1].semaphore = frame.timeline;
		signal_infos[1].value = signal_value;
		signal_infos[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

		VkCommandBufferSubmitInfo cmd_info{};
		cmd_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
		cmd_info.commandBuffer = cmd;

		VkSubmitInfo2 submit_info{};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
		submit_info.waitSemaphoreInfoCount = 2;
		submit_info.pWaitSemaphoreInfos = waits;
		submit_info.signalSemaphoreInfoCount = 2;
		submit_info.pSignalSemaphoreInfos = signal_infos;
		submit_info.commandBufferInfoCount = 1;
		submit_info.pCommandBufferInfos = &cmd_info;

		VkResult submit_result = vkQueueSubmit2(device.graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
		if (submit_result == VK_ERROR_DEVICE_LOST) { device.device_lost = true; return; }
		VK_ASSERT(submit_result);

		frame.frame_timeline_target[frame.current_frame] = signal_value;

		VkSwapchainKHR swapchains[] = { swapchain.swapchain };

		VkPresentInfoKHR present_info{};
		present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		present_info.waitSemaphoreCount = 1;
		present_info.pWaitSemaphores = signal_semaphores;
		present_info.swapchainCount = 1;
		present_info.pSwapchains = swapchains;
		present_info.pImageIndices = &swapchain.current_image_index;

		uint64_t id = 0;
        VkPresentIdKHR present_id_info{ VK_STRUCTURE_TYPE_PRESENT_ID_KHR, nullptr, 0, nullptr };
        if (device.capabilities.present_id) {
            id = ++frame.present_id;
            present_id_info.swapchainCount = 1;
            present_id_info.pPresentIds = &id;
            present_info.pNext = &present_id_info;
        }

		VkResult result = vkQueuePresentKHR(device.present_queue, &present_info);
		if (result == VK_ERROR_DEVICE_LOST) { device.device_lost = true; return; }
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
			recreate_swapchain(swapchain, device, surface, window);
			vulkan_frame_sync_ensure_present_semaphores(frame, device.device, static_cast<uint32_t>(swapchain.images.size()));
		}
	}

	void end_frame_rendering(FrameContext& frame, SwapchainContext& swapchain, DeviceContext& device, SurfaceContext& surface, WindowHandle window, VkCommandBuffer cmd, bool any_present, VkSemaphore transfer_timeline, uint64_t transfer_wait_value)
	{
	    if (any_present) {
    		VkImageMemoryBarrier2 end_barrier{};
    		end_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    		end_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    		end_barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    		end_barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    		end_barrier.dstAccessMask = 0;
    		end_barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    		end_barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    		end_barrier.image = swapchain.images[swapchain.current_image_index];
    		end_barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    		VkDependencyInfo dependency_info{};
    		dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    		dependency_info.dependencyFlags = 0;
    		dependency_info.memoryBarrierCount = 0;
    		dependency_info.pMemoryBarriers = NULL;
    		dependency_info.bufferMemoryBarrierCount = 0;
    		dependency_info.pBufferMemoryBarriers = NULL;
    		dependency_info.imageMemoryBarrierCount = 1;
    		dependency_info.pImageMemoryBarriers = &end_barrier;

    		vkCmdPipelineBarrier2(cmd, &dependency_info);
		}

		VK_ASSERT(vkEndCommandBuffer(cmd));

		handle_queues(frame, swapchain, device, surface, window, cmd, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, transfer_timeline, transfer_wait_value);
		frame.current_frame = (frame.current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
	}

	void transition_image(VkCommandBuffer cmd, VkImage image,
		VkImageLayout old_layout, VkImageLayout new_layout,
		VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
		VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access,
        VkImageAspectFlags aspect)
	{
		VkImageMemoryBarrier2 barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		barrier.srcStageMask = src_stage;
		barrier.srcAccessMask = src_access;
		barrier.dstStageMask = dst_stage;
		barrier.dstAccessMask = dst_access;
		barrier.oldLayout = old_layout;
		barrier.newLayout = new_layout;
		barrier.image = image;
		barrier.subresourceRange = { aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS };

		VkDependencyInfo dep{};
		dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dep.imageMemoryBarrierCount = 1;
		dep.pImageMemoryBarriers = &barrier;

		vkCmdPipelineBarrier2(cmd, &dep);
	}

	void end_frame_present(FrameContext& frame, SwapchainContext& swapchain, DeviceContext& device, SurfaceContext& surface, WindowHandle window, VkCommandBuffer cmd, VkSemaphore transfer_timeline, uint64_t transfer_wait_value)
	{
		VK_ASSERT(vkEndCommandBuffer(cmd));
		handle_queues(frame, swapchain, device, surface, window, cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, transfer_timeline, transfer_wait_value);
		frame.current_frame = (frame.current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
	}

	bool vulkan_wait_for_present(DeviceContext &device, SwapchainContext &swapchain, uint64_t present_id, uint64_t timeout_ns) {
        if (!device.capabilities.present_wait) return true;

        VkResult result = vkWaitForPresentKHR(device.device, swapchain.swapchain, present_id, timeout_ns);
		if (result == VK_TIMEOUT) return false;
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) return true;
		if (result < 0) return false;

		return true;
	}
}
