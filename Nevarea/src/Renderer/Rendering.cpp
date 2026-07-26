#include "lib/Rendering.hpp"
#include "RenderState.hpp"
#include "Renderer/Vulkan/VulkanContext.hpp"
#include "Renderer/Vulkan/VulkanFrames.hpp"
#include "Renderer/Vulkan/VulkanResourceManager.hpp"
#include "Renderer/Vulkan/VulkanSwapchain.hpp"
#include "Renderer/Vulkan/VulkanTranslate.hpp"
#include "lib/Logging.hpp"
#include "lib/WindowSystem.hpp"
#include <cstdio>
#include "Core/n_pch.hpp"

namespace Nevarea {
	namespace {
		constexpr uint32_t MAX_RENDERERS = 4;
		RenderState g_renderers[MAX_RENDERERS];
	}

	RenderState* resolve(RenderContext context) {
		uint32_t id = static_cast<uint32_t>(context);
		NEVAREA_ASSERT(id != 0 && id <= MAX_RENDERERS, "RENDERER", "Invalid RenderContext!");

		RenderState* render_state = &g_renderers[id - 1];
		NEVAREA_ASSERT(render_state->is_active, "RENDERER", "RenderContext refers to a destroyed renderer!");

		return render_state;
	}

	RenderContext renderer_create(RenderingAPI api)
	{
		for (uint32_t i = 0; i < MAX_RENDERERS; ++i) {
			if (g_renderers[i].is_active) continue;

			RenderState& render_state = g_renderers[i];
			render_state.is_active = true;
			render_state.api = api;

			switch (api) {
				case RenderingAPI::VULKAN:
					new (&render_state.vulkan) Renderer::VulkanContext {};
					break;

				case RenderingAPI::NONE:
			 		break;
			}

			return static_cast<RenderContext>(i + 1);
		}

		NEVAREA_ASSERT(false, "RENDERER", "MAX_RENDERERS exceeded!");
		return RenderContext::INVALID;
	}

	void renderer_destroy(RenderContext context)
	{
		RenderState* render_state = resolve(context);

		switch (render_state->api) {
			case RenderingAPI::VULKAN:
				Renderer::vulkan_context_destroy(render_state->vulkan);
				render_state->vulkan.~VulkanContext();
				break;

			case RenderingAPI::NONE:
				break;
		}

		render_state->is_active = false;
	}

	static SwapchainDescription create_swapchain_data(RenderContext context, const SwapchainDescription& swapchain_description) {
    	SwapchainDescription want = swapchain_description;
        RenderState* render_state = resolve(context);

        switch (render_state->api) {
            case RenderingAPI::VULKAN: {
                auto& vk = render_state->vulkan;
			    Renderer::SwapchainData swapchain_data{};

				if (!renderer_swapchain_format_supported(context, want.format, want.color_space)) {
                    NEVAREA_LOG(LogLevel::WARN, "Swapchain Format not supported! Fallbacking to default!");
                    want.format = Format::BGRA8_SRGB;
                    want.color_space = ColorSpace::SDR_SRGB;
                }

                if (!renderer_swapchain_present_mode_supported(context, want.present_mode)) {
                    NEVAREA_LOG(LogLevel::WARN, "Swapchain Present Mode not supported! Falling back to default!");
                    want.present_mode = PresentMode::VSYNC;
                }

                uint32_t count = want.image_count ? want.image_count : vk.surface.capabilities.minImageCount + 1;
                count = (std::max)(count, vk.surface.capabilities.minImageCount);

                if (vk.surface.capabilities.maxImageCount > 0)
                    count = (std::min)(count, vk.surface.capabilities.maxImageCount);

                want.image_count = count;

     			swapchain_data.present_mode = Renderer::to_vk_present_mode(want.present_mode);
                swapchain_data.color_space = Renderer::to_vk_color_space(want.color_space);
                swapchain_data.format = Renderer::to_vk_format(want.format);
                swapchain_data.image_count = want.image_count;
                swapchain_data.image_usage = Renderer::to_vk_image_usage(want.image_usage);

                vk.swapchain.data = swapchain_data;
                break;
            }

            case RenderingAPI::NONE: {
                break;
            }
        }

        return {};
	}

	SwapchainDescription renderer_hook_window(RenderContext context, WindowHandle window, const SwapchainDescription& swapchain_description)
	{
		RenderState* render_state = resolve(context);

		switch (render_state->api) {
			case RenderingAPI::VULKAN: {
			    auto& vk = render_state->vulkan;

    			Renderer::vulkan_context_init(vk, window);
                Renderer::query_swapchain_support(vk.device.physical_device, vk.surface);

			    SwapchainDescription want = create_swapchain_data(context, swapchain_description);

    			Renderer::vulkan_swapchain_init(vk.swapchain, vk.device, vk.surface, vk.window);
    			Renderer::vulkan_frame_sync_init(vk.frame_sync, vk.device, static_cast<uint32_t>(vk.swapchain.images.size()));
				return want;
			}
			case RenderingAPI::NONE:
				break;
		}

		return {};
	}

	void renderer_draw(RenderContext context)
	{
		RenderState* render_state = resolve(context);

		switch (render_state->api) {
			case RenderingAPI::VULKAN:
				Renderer::vulkan_context_draw(render_state->vulkan);
				break;

			case RenderingAPI::NONE:
				break;
		}
	}

	NvWinExtent renderer_get_swapchain_extent(RenderContext context) {
	    RenderState* render_state = resolve(context);

		switch (render_state->api) {
		    case RenderingAPI::VULKAN: {
				return { render_state->vulkan.swapchain.extent.width, render_state->vulkan.swapchain.extent.height };
			}

			case RenderingAPI::NONE: break;
		}

		return { 0, 0 };
	}

	void renderer_begin_pass(RenderContext context, const RenderPassDescription& description) {
	    RenderState* render_state = resolve(context);

		switch (render_state->api) {
		    case RenderingAPI::VULKAN: {
				Renderer::PassData pass{};
				pass.present = description.present;
				pass.depth = description.depth;
				pass.color.assign(description.color, description.color + description.color_count);
				Renderer::vulkan_begin_pass(render_state->vulkan, pass);
				break;
			}

			case RenderingAPI::NONE: break;
		}
	}

	void renderer_end_pass(RenderContext context) {
        RenderState* render_state = resolve(context);

        switch (render_state->api) {
            case RenderingAPI::VULKAN: {
                Renderer::vulkan_end_pass(render_state->vulkan);
                break;
            }

            case RenderingAPI::NONE: break;
        }
	}

	void renderer_request_device_extensions(RenderContext context, const char* const* names, uint32_t count) {
        RenderState* render_state = resolve(context);

        switch (render_state->api) {
            case RenderingAPI::VULKAN: {
                for (size_t i = 0; i < (size_t)count; i++)
                    render_state->vulkan.device.requested_extensions.push_back(names[i]);

                break;
            }

            case RenderingAPI::NONE: break;
        }
	}

	bool renderer_extension_supported(RenderContext renderer, const char* name) {
        RenderState* render_state = resolve(renderer);

        switch (render_state->api) {
            case RenderingAPI::VULKAN: {
                VkPhysicalDevice device = render_state->vulkan.device.physical_device;

                uint32_t count = 0;
                vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
                std::vector<VkExtensionProperties> props(count);
                vkEnumerateDeviceExtensionProperties(device, nullptr, &count, props.data());

                for (const VkExtensionProperties& extension : props) if (strcmp(extension.extensionName, name) == 0) return true;

                return false;
            }

            case RenderingAPI::NONE: break;
        }

        return false;
	}

	bool renderer_extension_enabled(RenderContext renderer, const char* name) {
        RenderState* render_state = resolve(renderer);

        switch (render_state->api) {
            case RenderingAPI::VULKAN: {
                for (const char* extension : render_state->vulkan.device.enabled_extensions)
                    if (strcmp(extension, name) == 0) return true;

                return false;
            }

            case RenderingAPI::NONE: break;
        }

        return false;
	}

	bool renderer_swapchain_resized(RenderContext context) {
	    RenderState* render_state = resolve(context);

		switch (render_state->api) {
		    case RenderingAPI::VULKAN: {
				bool was = render_state->vulkan.swapchain.resized;
				render_state->vulkan.swapchain.resized = false;
				return was;
			}

			case RenderingAPI::NONE: break;
		}

		return false;
	}

	NEVAREA_API SwapchainDescription renderer_update_swapchain(RenderContext context, const SwapchainDescription& description) {
	    RenderState* render_state = resolve(context);

		switch (render_state->api) {
		    case RenderingAPI::VULKAN: {
				auto& vk = render_state->vulkan;
                SwapchainDescription want = create_swapchain_data(context, description);
                Renderer::recreate_swapchain(vk.swapchain, vk.device, vk.surface, vk.window);
				Renderer::vulkan_frame_sync_ensure_present_semaphores(vk.frame_sync, vk.device.device, static_cast<uint32_t>(vk.swapchain.images.size()));
				return want;
			}

			case RenderingAPI::NONE:
			    break;
		}

		return {};
	}

	bool renderer_swapchain_format_supported(RenderContext context, Format format, ColorSpace color_space) {
        RenderState* render_state = resolve(context);

        switch (render_state->api) {
        case RenderingAPI::VULKAN: {
            auto& vk = render_state->vulkan;

            for (const auto& surface_format : vk.surface.supported_formats) {
    			if (surface_format.format == Renderer::to_vk_format(format)
                        && surface_format.colorSpace == Renderer::to_vk_color_space(color_space))

    				return true;
    		}

            return false;
        }

        case RenderingAPI::NONE:
            break;
        }

        return false;
	}

	bool renderer_swapchain_present_mode_supported(RenderContext context, PresentMode mode) {
        RenderState* render_state = resolve(context);

        switch (render_state->api) {
        case RenderingAPI::VULKAN: {
            auto& vk = render_state->vulkan;

            for (auto& present_mode : vk.surface.supported_present_modes) {
                if (present_mode == Renderer::to_vk_present_mode(mode))
                    return true;
            }

            return false;
        }

        case RenderingAPI::NONE:
            break;
        }

        return false;
	}
	Pipeline renderer_create_compute_pipeline(RenderContext context, const char* compute) {
		RenderState* render_state = resolve(context);

		switch (render_state->api) {
			case RenderingAPI::VULKAN: {
				Renderer::PipelineContext pipeline;

				Renderer::vulkan_compute_pipeline_init(pipeline,
					render_state->vulkan.device.device,
					render_state->vulkan.resource_manager.descriptor_layout,
					compute
				);

				return Renderer::vulkan_pipeline_add(render_state->vulkan, pipeline);
			}

			case RenderingAPI::NONE: {
				return {};
			}
		}

		return {};
	}

	Pipeline renderer_create_pipeline(RenderContext context, const PipelineDescription& description) {
		RenderState* render_state = resolve(context);

		switch (render_state->api) {
			case RenderingAPI::VULKAN: {
				Renderer::PipelineContext pipeline;

				Renderer::vulkan_pipeline_init(pipeline,
					render_state->vulkan.device.device,
					render_state->vulkan.swapchain.image_format,
					render_state->vulkan.resource_manager.descriptor_layout,
					description);

				return Renderer::vulkan_pipeline_add(render_state->vulkan, pipeline);
			}

			case RenderingAPI::NONE: {
				return {};
			}
		}

		return {};
	}

	void renderer_destroy_pipeline(RenderContext context, Pipeline pipeline) {
		RenderState* render_state = resolve(context);

		switch (render_state->api) {
			case RenderingAPI::VULKAN: {
				auto& vulkan_renderer = render_state->vulkan;
				Renderer::vulkan_pipeline_remove(vulkan_renderer, pipeline);
				break;
			}

			case RenderingAPI::NONE:
				break;
		}
	}

	AccelerationStructure renderer_create_acceleration_structure(RenderContext context, const AccelStructDescription& description) {
	    RenderState* render_state = resolve(context);
		if (render_state->api != RenderingAPI::VULKAN) return {};
		Renderer::AccelStructHandle handle = Renderer::vulkan_create_acceleration_structure(render_state->vulkan.resource_manager, description);
		return { handle.index, handle.generation };
	}

	uint64_t renderer_get_acceleration_structure_address(RenderContext context, AccelerationStructure acceleration_structure) {
        RenderState* render_state = resolve(context);
        if (render_state->api != RenderingAPI::VULKAN) return 0;
        return Renderer::vulkan_get_accel_address(render_state->vulkan.resource_manager, { acceleration_structure.id, acceleration_structure.generation });
	}

	void renderer_destroy_acceleration_structure(RenderContext context, AccelerationStructure acceleration_structure) {
        RenderState* render_state = resolve(context);
        if (render_state->api != RenderingAPI::VULKAN) return;
        Renderer::vulkan_destroy_acceleration_structure(render_state->vulkan.resource_manager, { acceleration_structure.id, acceleration_structure.generation }, render_state->vulkan.frame_sync);
	}

	Image renderer_create_image(RenderContext context, const ImageDescription& description) {
		RenderState* render_state = resolve(context);

		switch (render_state->api) {
			case RenderingAPI::VULKAN: {
				Renderer::ImageHandle handle = Renderer::vulkan_create_image(render_state->vulkan.resource_manager, description);
				return { handle.index, handle.generation };
			}
			case RenderingAPI::NONE:
				break;
		}
		return {};
	}

	void renderer_destroy_image(RenderContext context, Image handle) {
		RenderState* render_state = resolve(context);

		switch (render_state->api) {
			case RenderingAPI::VULKAN:
				Renderer::vulkan_destroy_image(render_state->vulkan.resource_manager, { handle.id, handle.generation }, render_state->vulkan.frame_sync);
				break;
			case RenderingAPI::NONE:
				break;
		}
	}

	Sampler renderer_create_sampler(RenderContext context, const SamplerDescription& description) {
    	RenderState* render_state = resolve(context);

    	switch (render_state->api) {
            case RenderingAPI::VULKAN: return Renderer::vulkan_create_sampler(render_state->vulkan.resource_manager, description);
            case RenderingAPI::NONE: break;
    	}

        return {};
	}

	void renderer_destroy_sampler(RenderContext context, Sampler sampler) {
	    RenderState* render_state = resolve(context);

		switch (render_state->api) {
		    case RenderingAPI::VULKAN: Renderer::vulkan_destroy_sampler(render_state->vulkan.resource_manager, sampler, render_state->vulkan.frame_sync);
			case RenderingAPI::NONE: break;
		};
	}

	void renderer_submit(RenderContext context, const DrawCommand& cmd) {
        RenderState* render_state = resolve(context);
        switch (render_state->api) {
            case RenderingAPI::VULKAN: Renderer::vulkan_submit(render_state->vulkan, cmd); break;
            case RenderingAPI::NONE: break;
        }
    }

	void renderer_dispatch_compute(RenderContext context, Pipeline pipeline, uint32_t groups_x, uint32_t groups_y, uint32_t groups_z, const void* push, size_t size) {
		RenderState* render_state = resolve(context);

		switch (render_state->api) {
    		case RenderingAPI::VULKAN: {
                Renderer::ComputeDispatch dispatch{ pipeline, groups_x, groups_y, groups_z };
                NEVAREA_ASSERT(size <= sizeof(dispatch.push_data), "RENDERER", "push data exceeds 128 bytes");
                memcpy(dispatch.push_data, push, size);
                dispatch.push_size = (uint32_t)size;
                render_state->vulkan.compute_dispatches.push_back(dispatch);

   			    break;
            }

    		case RenderingAPI::NONE:
    			break;
		}
	}

	void renderer_upload_image(RenderContext context, Image handle, const void* pixels, size_t size) {
        RenderState* render_state = resolve(context);

        switch (render_state->api) {
            case RenderingAPI::VULKAN: {
                Renderer::vulkan_upload_image(render_state->vulkan.resource_manager, { handle.id, handle.generation }, pixels, size);
                break;
            }

            case RenderingAPI::NONE:
                break;
        }
	}

	Buffer renderer_create_buffer(RenderContext context, const BufferDescription& description) {
        RenderState* render_state = resolve(context);

        switch (render_state->api) {
            case RenderingAPI::VULKAN: {
                Renderer::BufferHandle handle = Renderer::vulkan_create_buffer(render_state->vulkan.resource_manager, description);
                return { handle.index, handle.generation };
            }

            case RenderingAPI::NONE:
                break;
        }

        return { UINT32_MAX, 0 };
    }

    void renderer_destroy_buffer(RenderContext context, Buffer handle) {
        RenderState* render_state = resolve(context);

        switch (render_state->api) {
            case RenderingAPI::VULKAN:
                Renderer::vulkan_destroy_buffer(render_state->vulkan.resource_manager, { handle.id, handle.generation }, render_state->vulkan.frame_sync);
                break;

            case RenderingAPI::NONE:
                break;
        }
    }

    void renderer_update_buffer(RenderContext context, Buffer handle, const void* data, size_t size) {
        RenderState* render_state = resolve(context);

        switch (render_state->api) {
            case RenderingAPI::VULKAN: {
                auto& manager = render_state->vulkan.resource_manager;

                Renderer::BufferData& buffer_data = manager.buffers.get(handle.id, handle.generation);

                VmaAllocationInfo alloc_info{};
                vmaGetAllocationInfo(manager.allocator, buffer_data.allocation, &alloc_info);
                NEVAREA_ASSERT(size <= alloc_info.size, "RESOURCE MANAGER", "renderer_update_buffer: size exceeds buffer allocation!");

                VkMemoryPropertyFlags mem_flags;
                vmaGetAllocationMemoryProperties(manager.allocator, buffer_data.allocation, &mem_flags);

                if (mem_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
                    void* mapped_memory = nullptr;
                    VK_ASSERT(vmaMapMemory(manager.allocator, buffer_data.allocation, &mapped_memory));
                    memcpy(mapped_memory, data, size);
                    vmaUnmapMemory(manager.allocator, buffer_data.allocation);
                } else
                    Renderer::vulkan_upload_buffer(manager, { handle.id, handle.generation }, data, size);

                break;
            }

            case RenderingAPI::NONE:
                break;
        }
    }

    uint64_t renderer_get_buffer_address(RenderContext context, Buffer handle) {
        RenderState* render_state = resolve(context);

        switch (render_state->api) {
            case RenderingAPI::VULKAN:
                return Renderer::vulkan_get_buffer_address(render_state->vulkan.resource_manager, { handle.id, handle.generation });

            case RenderingAPI::NONE:
                break;
        }

        return 0;
    }

   	bool renderer_device_lost(RenderContext context) {
		RenderState* render_state = resolve(context);

		switch (render_state->api) {
			case RenderingAPI::VULKAN: return render_state->vulkan.device.device_lost;
			case RenderingAPI::NONE: break;
		}

		return false;
	}

	uint64_t renderer_last_present_id(RenderContext context) {
        RenderState* render_state = resolve(context);

        switch (render_state->api) {
            case Nevarea::RenderingAPI::VULKAN: return render_state->vulkan.frame_sync.present_id;
            case Nevarea::RenderingAPI::NONE: break;
        }

        return 0;
	}

	bool renderer_wait_for_present(RenderContext context, uint64_t present_id, uint64_t timeout_ns) {
        RenderState* render_state = resolve(context);

        switch (render_state->api) {
            case Nevarea::RenderingAPI::VULKAN: {
                auto& vk = render_state->vulkan;

                return Renderer::vulkan_wait_for_present(vk.device, vk.swapchain, present_id, timeout_ns);
            }
            case Nevarea::RenderingAPI::NONE: break;
        }

        return false;
	}
}
