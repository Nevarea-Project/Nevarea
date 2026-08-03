#include "VulkanContext.hpp"
#include "Renderer/Vulkan/VulkanResourceManager.hpp"
#include "VulkanDebug.hpp"
#include "VulkanSpec.hpp"
#include "VulkanFrames.hpp"

#include "Core/n_pch.hpp"
#include "lib/Core.hpp"
#include <lib/Rendering.hpp>

#define NEVAREA_VULKAN_VERSION VK_API_VERSION_1_4

namespace Nevarea::Renderer {
	static void vulkan_context_create_instance(VkInstance& instance)
	{
		VkApplicationInfo app_info{};
		app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		app_info.pNext = nullptr;
		app_info.pApplicationName = "Nevarea Application";
		app_info.pEngineName = "No Engine";
		app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
		app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
		app_info.apiVersion = NEVAREA_VULKAN_VERSION;

		u32 extension_count = 0;
		VK_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr));

		std::vector<VkExtensionProperties> avaliable_extensions(extension_count);
		VK_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, avaliable_extensions.data()));

		std::set<std::string> available;
		for (const auto& extension : avaliable_extensions)
			available.insert(extension.extensionName);

		std::vector<const char*> enabled_extensions;
		for (const char* extension : required_instance_extensions) {
			NEVAREA_ASSERT(available.count(extension) > 0, "VULKAN INSTANCE", "Required instance extension not available!");
			enabled_extensions.push_back(extension);
		}

		for (const char* extension : optional_instance_extensions) {
			if (available.count(extension) > 0) enabled_extensions.push_back(extension);
			else NEVAREA_LOG(LogLevel::WARN, "optional instance extension: '%s' not avaliable, skipping\n", extension);
		}

		VkDebugUtilsMessengerCreateInfoEXT debug_create_info{};
		populate_debug_create_info(debug_create_info);

		VkInstanceCreateInfo instance_create_info{};
		instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		instance_create_info.pNext = nullptr;
		instance_create_info.flags = 0;
		instance_create_info.pApplicationInfo = &app_info;
		instance_create_info.enabledLayerCount = 0;
		instance_create_info.ppEnabledLayerNames = nullptr;
		instance_create_info.enabledExtensionCount = static_cast<u32>(enabled_extensions.size());
		instance_create_info.ppEnabledExtensionNames = enabled_extensions.data();

		#ifdef NEVAREA_DEBUG
			u32 available_count = 0;
			VK_CHECK(vkEnumerateInstanceLayerProperties(&available_count, nullptr));
			std::vector<VkLayerProperties> available_layers(available_count);
			VK_CHECK(vkEnumerateInstanceLayerProperties(&available_count, available_layers.data()));

			std::vector<const char*> enabled_layers;
			for (const char* requested : validation_layers) {
				bool found = false;
				for (const auto& layer : available_layers)
					if (strcmp(layer.layerName, requested) == 0) { found = true; break; }

				if (found) enabled_layers.push_back(requested);
				else NEVAREA_LOG(LogLevel::WARN, "validation layer: '%s' is not avaliable, skipping\n", requested);
			}

			instance_create_info.enabledLayerCount = static_cast<u32>(enabled_layers.size());
			instance_create_info.ppEnabledLayerNames = enabled_layers.data();
			if (!enabled_layers.empty())
				instance_create_info.pNext = &debug_create_info;
		#endif

		VK_ASSERT(vkCreateInstance(&instance_create_info, nullptr, &instance));
	}

	static void vulkan_context_debug_messenger(VkInstance instance, VkDebugUtilsMessengerEXT& debug_messenger)
	{
		#ifdef NEVAREA_DEBUG
		bool debug = true;
		#else
		bool debug = false;
		#endif

		if (!debug) return;

		VkDebugUtilsMessengerCreateInfoEXT create_info{};
		populate_debug_create_info(create_info);

		VK_ASSERT(create_debug_utils_messenger_ext(instance, &create_info, nullptr, &debug_messenger));
	}

	static void vulkan_context_create_surface(WindowHandle window, VkInstance instance, VkSurfaceKHR& surface)
	{
		#ifdef NEVAREA_PLATFORM_WINDOWS
			VkWin32SurfaceCreateInfoKHR create_info{};
			create_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
			create_info.hwnd = window_get_hwnd(window);
			create_info.hinstance = window_get_hinstance(window);

			VK_ASSERT(vkCreateWin32SurfaceKHR(instance, &create_info, nullptr, &surface));
		#endif
	}

	static void vulkan_context_create_allocator(VkInstance instance, VkPhysicalDevice physical_device, VkDevice device, VmaAllocator& allocator, bool enable_memory_priority) {
	    VmaAllocatorCreateInfo create_info{};
		create_info.instance = instance;
		create_info.device = device;
		create_info.physicalDevice = physical_device;
		create_info.vulkanApiVersion = NEVAREA_VULKAN_VERSION;
		create_info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT | (enable_memory_priority ? VMA_ALLOCATOR_CREATE_EXT_MEMORY_PRIORITY_BIT : 0);

		VmaVulkanFunctions vk_funcs{};
        vk_funcs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        vk_funcs.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
        create_info.pVulkanFunctions = &vk_funcs;

		VK_ASSERT(vmaCreateAllocator(&create_info, &allocator));
	}

	void vulkan_context_init(VulkanContext& context, WindowHandle window) {
		context.window = window;

		VK_ASSERT(volkInitialize());

		vulkan_context_create_instance(context.instance);
		volkLoadInstance(context.instance);
		vulkan_context_debug_messenger(context.instance, context.debug_messenger);
		vulkan_context_create_surface(context.window, context.instance, context.surface.surface);
		vulkan_device_init(context.device, context.instance, context.surface.surface);
		volkLoadDevice(context.device.device);
		vulkan_context_create_allocator(context.instance, context.device.physical_device, context.device.device, context.allocator, context.device.capabilities.memory_priority);
		vulkan_resources_init(context.resource_manager, context.allocator, context.device);
	}

	static void transition_tracked(VkCommandBuffer cmd, AllocatedImage& img, VkImageLayout new_layout,
		VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
		VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access)
	{
	    VkImageAspectFlags aspect = k_format_info[(u32)img.format].aspect;
		transition_image(cmd, img.image, img.current_layout, new_layout, src_stage, src_access, dst_stage, dst_access, aspect);
		img.current_layout = new_layout;
	}

	static void reclaim_transfers(ResourceManager& manager) {
	    uint64_t done = 0;
		vkGetSemaphoreCounterValue(manager.device, manager.transfer_timeline, &done);

		auto& pending = manager.pending_uploads;
		for (size_t i = 0; i < pending.size();) {
		    if (pending[i].value <= done) {
				vmaDestroyBuffer(manager.allocator, pending[i].staging, pending[i].allocation);
				manager.free_transfer_cmds.push_back(pending[i].cmd);
				pending[i] = pending.back();
				pending.pop_back();
			} else ++i;
		}
	}

	void vulkan_context_draw(VulkanContext& context) {
	    if (context.device.device_lost) return;

	    VkSurfaceCapabilitiesKHR caps;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context.device.physical_device, context.surface.surface, &caps);
        if (caps.currentExtent.width == 0 || caps.currentExtent.height == 0) {
            context.compute_dispatches.clear();
            context.passes.clear();
            context.interop_records.clear();

            return;
        }

        reclaim_transfers(context.resource_manager);

		VkCommandBuffer cmd = begin_frame(context.frame_sync, context.swapchain, context.device, context.surface, context.window);
		if (cmd == VK_NULL_HANDLE) return;

		bool had_compute = !context.compute_dispatches.empty();

		for (size_t i = 0; i < context.compute_dispatches.size(); i++) {
			const ComputeDispatch& dispatch = context.compute_dispatches[i];
			const PipelineContext& pipeline = vulkan_pipeline_get(context, dispatch.pipeline);

			vkCmdBindPipeline(cmd, pipeline.bind_point, pipeline.pipeline);
			vkCmdBindDescriptorSets(cmd, pipeline.bind_point, pipeline.layout, 0, 1, &context.resource_manager.descriptor_set, 0, nullptr);
			vkCmdPushConstants(cmd, pipeline.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, dispatch.push_size, dispatch.push_data);
			vkCmdDispatch(cmd, dispatch.groups_x, dispatch.groups_y, dispatch.groups_z);

			if (i + 1 < context.compute_dispatches.size()) {
				VkMemoryBarrier2 barrier{};
				barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
				barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
				barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
				barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
				barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;

				VkDependencyInfo dep{};
				dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
				dep.memoryBarrierCount = 1;
				dep.pMemoryBarriers = &barrier;
				vkCmdPipelineBarrier2(cmd, &dep);
			}
		}
		context.compute_dispatches.clear();

		if (had_compute) {
			VkMemoryBarrier2 barrier{};
			barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
			barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
			barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
			barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
			barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;

			VkDependencyInfo dependency{};
			dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
			dependency.memoryBarrierCount = 1;
			dependency.pMemoryBarriers = &barrier;

			vkCmdPipelineBarrier2(cmd, &dependency);
		}

		VkViewport viewport{};
		viewport.width = static_cast<float>(context.swapchain.extent.width);
		viewport.height = static_cast<float>(context.swapchain.extent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		VkRect2D scissor{ {0, 0}, context.swapchain.extent };

		bool any_present = false;

		auto fire_interop = [&](u32 boundary) {
            for (InteropRecord& record : context.interop_records)
                if (!record.in_scope && record.at == boundary) record.fn(cmd, record.user);
        };

		u32 pass_index = 0;
		fire_interop(pass_index);
		for (PassData& pass : context.passes) {
		    for (size_t i = 0; i < pass.color.size(); i++) {
                if (pass.present && i == 0) {
                    transition_image(cmd, context.swapchain.images[context.swapchain.current_image_index],
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
                    any_present = true;
                } else {
                    transition_tracked(cmd, vulkan_get_image(context.resource_manager, { pass.color[i].image.id, pass.color[i].image.generation }),
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
                }
            }

    		if (pass.depth.image.is_valid()) {
                transition_tracked(cmd, vulkan_get_image(context.resource_manager, { pass.depth.image.id, pass.depth.image.generation }),
                    VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                    VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
    		}

    		begin_rendering(cmd, pass, context.swapchain, context.resource_manager);
    		vkCmdSetViewport(cmd, 0, 1, &viewport);
    		vkCmdSetScissor(cmd, 0, 1, &scissor);

    		for (DrawBucket& bucket : pass.buckets) {
    			if (bucket.items.empty()) continue;

    			const PipelineContext& pipeline = vulkan_pipeline_get(context, bucket.pipeline);

    			vkCmdBindPipeline(cmd, pipeline.bind_point, pipeline.pipeline);
    			vkCmdBindDescriptorSets(cmd, pipeline.bind_point, pipeline.layout, 0, 1, &context.resource_manager.descriptor_set, 0, nullptr);

    			for (const DrawItem& item : bucket.items) {
                    if (item.push_size)
                        vkCmdPushConstants(cmd, pipeline.layout,
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                            0, item.push_size, item.push_data);

                    if (item.index_buffer.is_valid()) {
                        VkBuffer index_buffer = vulkan_get_buffer(context.resource_manager, item.index_buffer);
                        vkCmdBindIndexBuffer(cmd, index_buffer, 0, VK_INDEX_TYPE_UINT32);
                        vkCmdDrawIndexed(cmd, item.count, item.instance_count, item.first, item.vertex_offset, 0);
                    } else {
                        vkCmdDraw(cmd, item.count, item.instance_count, item.first, 0);
                    }
                }
    			bucket.items.clear();
    		}

            for (InteropRecord& record : context.interop_records)
                if (record.in_scope && record.at == pass_index) record.fn(cmd, record.user);

            vkCmdEndRendering(cmd);

            for (size_t i = 0; i < pass.color.size(); i++) {
                if (pass.present && i == 0) continue;
                transition_tracked(cmd, vulkan_get_image(context.resource_manager, { pass.color[i].image.id, pass.color[i].image.generation }),
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            }

            fire_interop(++pass_index);
		}
		context.passes.clear();
		context.interop_records.clear();

		end_frame_rendering(context.frame_sync, context.swapchain, context.device,
	        context.surface, context.window, cmd, any_present,
		    context.resource_manager.transfer_timeline,
		    context.resource_manager.transfer_value);
	}

	void vulkan_context_destroy(VulkanContext& context)
	{
		vkDeviceWaitIdle(context.device.device);

		for (PipelineContext& pipeline : context.pipelines.data)
		    if (pipeline.pipeline != VK_NULL_HANDLE)
				vulkan_pipeline_destroy(pipeline, context.device.device, context.frame_sync);

		for (auto& queue : context.frame_sync.deletion_queues) vulkan_resources_flush_deletors(queue);

		vulkan_frame_sync_destroy(context.frame_sync, context.device.device);
		vulkan_swapchain_destroy(context.swapchain, context.device.device);

		vulkan_resources_destroy(context.resource_manager);
		vmaDestroyAllocator(context.allocator);

		vulkan_device_destroy(&context.device);

		#ifdef NEVAREA_DEBUG
		destroy_debug_utils_messenger_ext(context.instance, context.debug_messenger, nullptr);
		#endif // NEVAREA_DEBUG

		vkDestroySurfaceKHR(context.instance, context.surface.surface, nullptr);
		vkDestroyInstance(context.instance, nullptr);
	}

	static DrawBucket& get_bucket(VulkanContext& context, Pipeline pipeline) {
    	NEVAREA_ASSERT(context.current_pass_index >= 0, "RENDERER", "submit_mesh called outside begin_pass/end_pass");

        PassData& pass = context.passes[context.current_pass_index];
        if (pipeline.id >= pass.buckets.size()) pass.buckets.resize(pipeline.id + 1);

        DrawBucket& bucket = pass.buckets[pipeline.id];
        bucket.pipeline = pipeline;
        return bucket;
    }

    void vulkan_submit(VulkanContext& context, const DrawCommand& cmd) {
        DrawItem item{};
        item.index_buffer = { cmd.index_buffer.id, cmd.index_buffer.generation };
        item.count = cmd.count;
        item.first = cmd.first;
        item.vertex_offset = cmd.vertex_offset;
        item.instance_count = cmd.instance_count;
        item.push_size = (u32)cmd.push_size;

        if (cmd.push && cmd.push_size) {
            NEVAREA_ASSERT(cmd.push_size <= NEVAREA_MAX_PUSH_CONSTANTS_SIZE, "RENDERER", "push too large");
            memcpy(item.push_data, cmd.push, cmd.push_size);
        }

        get_bucket(context, cmd.pipeline).items.push_back(item);
    }

	void vulkan_begin_pass(VulkanContext& context, PassData pass) {
	    context.passes.push_back(pass);
		context.current_pass_index = (int32_t)context.passes.size() - 1;
	}

	void vulkan_end_pass(VulkanContext& context) {
	    context.current_pass_index = -1;
	}

	Pipeline vulkan_pipeline_add(VulkanContext &context, const PipelineContext &pipeline) {
	    u32 index = context.pipelines.add(pipeline);
		return { index, context.pipelines.generations[index] };
	}

	PipelineContext& vulkan_pipeline_get(VulkanContext& context, Pipeline handle) {
		return context.pipelines.get(handle.id, handle.generation);
	}

	void vulkan_pipeline_remove(VulkanContext& context, Pipeline handle) {
		PipelineContext& pipeline = vulkan_pipeline_get(context, handle);
		vulkan_pipeline_destroy(pipeline, context.device.device, context.frame_sync);
		context.pipelines.remove(handle.id);
	}
}
