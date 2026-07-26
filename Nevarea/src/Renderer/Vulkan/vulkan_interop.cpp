#include "interop/vulkan_interop.hpp"
#include "Renderer/RenderState.hpp"

namespace Nevarea {
    VulkanHandles renderer_vk_context(RenderContext context) {
        Renderer::VulkanContext& vk = resolve(context)->vulkan;

         return { vk.instance, vk.device.physical_device, vk.device.device,
                  vk.device.graphics_queue, vk.device.graphics_family_index };
    }

    VulkanSwapchainInfo renderer_vk_swapchain_info(RenderContext context) {
        Renderer::VulkanContext& vk = resolve(context)->vulkan;
        uint32_t count = (uint32_t)vk.swapchain.images.size();
        return { vk.swapchain.image_format, count, count };
    }

    PFN_vkGetInstanceProcAddr renderer_vk_instance_proc_addr(RenderContext) {
        return ::vkGetInstanceProcAddr;
    }

    VkDescriptorPool renderer_vk_create_descriptor_pool(RenderContext context, const VkDescriptorPoolCreateInfo* info) {
        Renderer::VulkanContext& vk = resolve(context)->vulkan;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        vkCreateDescriptorPool(vk.device.device, info, nullptr, &pool);
        return pool;
    }

    void renderer_vk_destroy_descriptor_pool(RenderContext context, VkDescriptorPool pool) {
        Renderer::VulkanContext& vk = resolve(context)->vulkan;
        vkDestroyDescriptorPool(vk.device.device, pool, nullptr);
    }

    void renderer_vk_device_wait_idle(RenderContext context) {
        vkDeviceWaitIdle(resolve(context)->vulkan.device.device);
    }

    VulkanImage renderer_vk_image(RenderContext context, Image image) {
        Renderer::VulkanContext& vk = resolve(context)->vulkan;
        Renderer::AllocatedImage& img = Renderer::vulkan_get_image(vk.resource_manager, { image.id, image.generation });

        return { img.image, img.view, Renderer::to_vk_format(img.format),
                 img.extent, 1, 1, img.current_layout, img.usage };
    }

    VulkanBuffer renderer_vk_buffer(RenderContext context, Buffer buffer) {
        Renderer::ResourceManager& resources = resolve(context)->vulkan.resource_manager;
        Renderer::BufferHandle handle{ buffer.id, buffer.generation };

        VmaAllocationInfo info{};
        vmaGetAllocationInfo(resources.allocator, resources.buffers.data[buffer.id].allocation, &info);

        return { Renderer::vulkan_get_buffer(resources, handle), info.size, Renderer::vulkan_get_buffer_address(resources, handle) };
    }

    VkImageLayout renderer_vk_image_layout(RenderContext context, Image image) {
        return Renderer::vulkan_get_image(resolve(context)->vulkan.resource_manager,
            { image.id, image.generation }).current_layout;
    }

    void renderer_vk_set_image_layout(RenderContext context, Image image, VkImageLayout image_layout) {
        Renderer::vulkan_get_image(resolve(context)->vulkan.resource_manager,
            { image.id, image.generation }).current_layout = image_layout;
    }

    void renderer_vk_immediate_submit(RenderContext context, void (*record)(VkCommandBuffer, void*), void* user) {
        Renderer::vulkan_immediate_submit(resolve(context)->vulkan.resource_manager,
            [record, user](VkCommandBuffer cmd) { record(cmd, user); });
    }

    VkSemaphore renderer_vk_timeline(RenderContext context, uint64_t* out_last_value) {
        Renderer::FrameContext& frame = resolve(context)->vulkan.frame_sync;
        if (out_last_value) *out_last_value = frame.timeline_value;
        return frame.timeline;
    }

    void renderer_vk_record_inline(RenderContext context, void (*fn)(VkCommandBuffer cmd, void* user), void* user) {
        Renderer::VulkanContext& vk = resolve(context)->vulkan;
        vk.interop_records.push_back({ fn, user, (uint32_t)vk.passes.size(), false });
    }

    void renderer_vk_record_in_pass(RenderContext context, uint32_t pass_index, void (*fn)(VkCommandBuffer cmd, void* user), void* user) {
        Renderer::VulkanContext& vk = resolve(context)->vulkan;
        vk.interop_records.push_back({ fn, user, pass_index, true });
    }

    void renderer_vk_request_features(RenderContext context, const void* feature_chain) {
        Renderer::VulkanContext& vk = resolve(context)->vulkan;
        vk.device.user_feature_chain = feature_chain;
    }
}
