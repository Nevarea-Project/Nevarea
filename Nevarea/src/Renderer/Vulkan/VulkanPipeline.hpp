#pragma once

#include "VulkanSwapchain.hpp"

namespace Nevarea::Renderer {
    struct PipelineContext {
        VkPipeline pipeline;
        VkPipelineLayout layout;
        VkPipelineBindPoint bind_point;
    };

    void vulkan_compute_pipeline_init(PipelineContext& pipeline, VkDevice device, VkDescriptorSetLayout descriptor_layout, const char* compute);
    void vulkan_pipeline_init(PipelineContext& pipeline, VkDevice device, VkFormat color_format, VkDescriptorSetLayout descriptor_layout, const PipelineDescription& description);
    void vulkan_pipeline_destroy(PipelineContext& pipeline, VkDevice device, FrameContext& frame);
}
