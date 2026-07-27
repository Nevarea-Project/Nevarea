#pragma once

#include "lib/Rendering.hpp"
#include <vulkan/vulkan.h>

namespace Nevarea {
    struct VulkanHandles {
        VkInstance instance;
        VkPhysicalDevice physical_device;
        VkDevice device;
        VkQueue graphics_queue;
        u32 graphics_family;
    };

    struct VulkanImage {
        VkImage image;
        VkImageView image_view;
        VkFormat format;
        VkExtent2D extent;
        u32 mips, layers;
        VkImageLayout current_layout;
        VkImageUsageFlags usage;
    };

    struct VulkanBuffer {
        VkBuffer buffer;
        VkDeviceSize size;
        VkDeviceAddress address;
    };

    struct VulkanSwapchainInfo {
        VkFormat color_format;
        u32 image_count;
        u32 min_image_count;
    };

    NEVAREA_API VulkanHandles renderer_vk_context(RenderContext context);
    NEVAREA_API VulkanSwapchainInfo renderer_vk_swapchain_info(RenderContext context);

    NEVAREA_API PFN_vkGetInstanceProcAddr renderer_vk_instance_proc_addr(RenderContext context);
    NEVAREA_API VkDescriptorPool renderer_vk_create_descriptor_pool(RenderContext context, const VkDescriptorPoolCreateInfo* info);
    NEVAREA_API void renderer_vk_destroy_descriptor_pool(RenderContext context, VkDescriptorPool pool);
    NEVAREA_API void renderer_vk_device_wait_idle(RenderContext context);
    NEVAREA_API VulkanImage renderer_vk_image(RenderContext context, Image image);
    NEVAREA_API VulkanBuffer renderer_vk_buffer(RenderContext context, Buffer buffer);

    NEVAREA_API VkImageLayout renderer_vk_image_layout(RenderContext context, Image image);
    NEVAREA_API void renderer_vk_set_image_layout(RenderContext context, Image image, VkImageLayout image_layout);

    NEVAREA_API void renderer_vk_immediate_submit(RenderContext context, void (*record)(VkCommandBuffer cmd, void* user), void* user);
    NEVAREA_API VkSemaphore renderer_vk_timeline(RenderContext context, uint64_t* out_last_value);
    NEVAREA_API void renderer_vk_record_inline(RenderContext context, void (*fn)(VkCommandBuffer cmd, void* user), void* user);
    NEVAREA_API void renderer_vk_record_in_pass(RenderContext context, uint32_t pass_index, void (*fn)(VkCommandBuffer cmd, void* user), void* user);

    NEVAREA_API void renderer_vk_request_features(RenderContext context, const void* feature_chain);
}
