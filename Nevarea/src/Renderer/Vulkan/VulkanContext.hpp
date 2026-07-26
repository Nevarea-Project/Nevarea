#pragma once

#include "Platform/WindowSystemInternal.hpp"
#include "Core/n_pch.hpp"

#include "VulkanDevice.hpp"
#include "VulkanSwapchain.hpp"
#include "VulkanResourceManager.hpp"
#include "VulkanPipeline.hpp"
#include "lib/Rendering.hpp"

#include <vk_mem_alloc.h>

namespace Nevarea::Renderer {
    struct DrawItem {
        BufferHandle index_buffer;
        uint32_t count, first, instance_count;
        int32_t vertex_offset;
        uint8_t push_data[NEVAREA_MAX_PUSH_CONSTANTS_SIZE];
        uint32_t push_size;
    };

	struct DrawBucket {
		Pipeline pipeline;
		std::vector<DrawItem> items;
	};

	struct ComputeDispatch {
		Pipeline pipeline;
		uint32_t groups_x, groups_y, groups_z;

		uint8_t push_data[NEVAREA_MAX_PUSH_CONSTANTS_SIZE];
		uint32_t push_size;
	};

	struct PassData {
	    bool present = false;
		std::vector<ColorAttachment> color;
		DepthAttachment depth;
		std::vector<DrawBucket> buckets;
	};

	struct InteropRecord {
	    void (*fn)(VkCommandBuffer cmd, void* user);
		void* user;
		uint32_t at;
		bool in_scope;
	};

	struct VulkanContext {
		VkInstance instance;
		VkDebugUtilsMessengerEXT debug_messenger;
		VmaAllocator allocator;

		ResourceManager resource_manager;
		WindowHandle window;
		DeviceContext device;
		SurfaceContext surface;
		SwapchainContext swapchain;
		FrameContext frame_sync;

		std::vector<InteropRecord> interop_records;

		Pool<PipelineContext> pipelines;

		std::vector<PassData> passes;
		int32_t current_pass_index = -1;

		std::vector<ComputeDispatch> compute_dispatches;
	};

	void vulkan_context_init(VulkanContext& context, WindowHandle window);
	void vulkan_context_draw(VulkanContext& context);
	void vulkan_context_destroy(VulkanContext& context);

	void vulkan_submit(VulkanContext& context, const DrawCommand& cmd);

	void vulkan_begin_pass(VulkanContext& context, PassData pass);
	void vulkan_end_pass(VulkanContext& context);

	Pipeline vulkan_pipeline_add(VulkanContext& context, const PipelineContext& pipeline);
	PipelineContext& vulkan_pipeline_get(VulkanContext& context, Pipeline handle);
	void vulkan_pipeline_remove(VulkanContext& context, Pipeline handle);
}
