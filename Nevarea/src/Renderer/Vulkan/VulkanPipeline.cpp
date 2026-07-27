#include "Core/n_pch.hpp"

#include "VulkanPipeline.hpp"
#include "VulkanFrames.hpp"
#include "VulkanResourceManager.hpp"
#include "VulkanDebug.hpp"
#include "VulkanTranslate.hpp"

using FileData = std::vector<char>;

namespace Nevarea::Renderer {
	FileData read_file(const std::string& filepath) {
		std::ifstream file(filepath, std::ios::ate | std::ios::binary);

		if (!file.is_open()) {
			NEVAREA_ASSERT(false, "VULKAN PIPELINE", "Failed to open shader file!");
			return {};
		}

		size_t fileSize = static_cast<size_t>(file.tellg());
		std::vector<char> buffer(fileSize);

		file.seekg(0);
		file.read(buffer.data(), fileSize);

		file.close();

		return buffer;
	}

	void create_shader_module(VkDevice device, FileData shader_code, VkShaderModule* shader_module) {
		VkShaderModuleCreateInfo create_info{};
		create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		create_info.codeSize = shader_code.size();
		create_info.pCode = reinterpret_cast<const u32*>(shader_code.data());

		VK_ASSERT(vkCreateShaderModule(device, &create_info, nullptr, shader_module));
	}

	void vulkan_compute_pipeline_init(PipelineContext& pipeline, VkDevice device, VkDescriptorSetLayout descriptor_layout, const char* compute)
	{
		FileData compute_code = read_file(compute);
		VkShaderModule compute_shader;

		create_shader_module(device, compute_code, &compute_shader);

		VkPipelineShaderStageCreateInfo stage{};
		stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		stage.module = compute_shader;
		stage.pName = "main";

		VkPushConstantRange push_range{};
		push_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		push_range.offset = 0;
		push_range.size = NEVAREA_MAX_PUSH_CONSTANTS_SIZE;

		VkPipelineLayoutCreateInfo layout_info{};
		layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layout_info.setLayoutCount = 1;
		layout_info.pSetLayouts = &descriptor_layout;
		layout_info.pushConstantRangeCount = 1;
		layout_info.pPushConstantRanges = &push_range;

		VK_ASSERT(vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline.layout));
		VK_NAME(device, VK_OBJECT_TYPE_PIPELINE_LAYOUT, pipeline.layout, compute);

		VkComputePipelineCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		info.stage = stage;
		info.layout = pipeline.layout;

		VK_ASSERT(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline.pipeline));
		VK_NAME(device, VK_OBJECT_TYPE_PIPELINE, pipeline.pipeline, compute);

		vkDestroyShaderModule(device, compute_shader, nullptr);

		pipeline.bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;
	}

	void vulkan_pipeline_init(PipelineContext& pipeline, VkDevice device, VkFormat color_format, VkDescriptorSetLayout descriptor_layout, const PipelineDescription& desc) {
		FileData vert_code = read_file(desc.vertex_shader);
		FileData frag_code = read_file(desc.fragment_shader);

		VkShaderModule vert_shader;
		VkShaderModule frag_shader;

		create_shader_module(device, vert_code, &vert_shader);
		create_shader_module(device, frag_code, &frag_shader);

		VkPushConstantRange push_constant_range{};
		push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		push_constant_range.offset = 0;
		push_constant_range.size = NEVAREA_MAX_PUSH_CONSTANTS_SIZE;

		VkPipelineLayoutCreateInfo layout_info{};
		layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layout_info.pushConstantRangeCount = 1;
		layout_info.pPushConstantRanges = &push_constant_range;
		layout_info.setLayoutCount = 1;
		layout_info.pSetLayouts = &descriptor_layout;

		VK_ASSERT(vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline.layout));
		VK_NAME(device, VK_OBJECT_TYPE_PIPELINE_LAYOUT, pipeline.layout, desc.vertex_shader);

		VkPipelineShaderStageCreateInfo shader_stages[2]{};
		shader_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		shader_stages[0].module = vert_shader;
		shader_stages[0].pName = "main";

		shader_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		shader_stages[1].module = frag_shader;
		shader_stages[1].pName = "main";

		VkPipelineVertexInputStateCreateInfo vertex_input{};
		vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

		VkPipelineInputAssemblyStateCreateInfo input_assembly{};
		input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		input_assembly.topology = to_vk_topology(desc.topology);

		VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamic_state{};
		dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamic_state.dynamicStateCount = 2;
		dynamic_state.pDynamicStates = dynamic_states;

		VkPipelineViewportStateCreateInfo viewport_state{};
		viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewport_state.viewportCount = 1;
		viewport_state.scissorCount = 1;

		VkPipelineRasterizationStateCreateInfo rasterization{};
		rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterization.polygonMode = to_vk_polygon_mode(desc.polygon_mode);
		rasterization.cullMode = to_vk_cull_mode(desc.cull_mode);
		rasterization.frontFace = to_vk_front_face(desc.front_face);
		rasterization.lineWidth = 1.0f;

		VkPipelineMultisampleStateCreateInfo multisample{};
		multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineColorBlendAttachmentState blend_attachment{};
		blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		blend_attachment.blendEnable = desc.blend_enable ? VK_TRUE : VK_FALSE;
        if (desc.blend_enable) {
            blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend_attachment.colorBlendOp        = VK_BLEND_OP_ADD;
            blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            blend_attachment.alphaBlendOp        = VK_BLEND_OP_ADD;
        }

		VkPipelineColorBlendStateCreateInfo color_blend{};
		color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		color_blend.attachmentCount = 1;
		color_blend.pAttachments = &blend_attachment;

		VkFormat resolved_color = (desc.color_format == Format::COUNT) ? color_format : to_vk_format(desc.color_format);

		VkPipelineRenderingCreateInfo rendering_info{};
		rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
		rendering_info.colorAttachmentCount = 1;
		rendering_info.pColorAttachmentFormats = &resolved_color;
		rendering_info.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;

		bool has_depth = desc.depth_format != Format::COUNT;
        VkPipelineDepthStencilStateCreateInfo depth_stencil{};
        depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth_stencil.depthTestEnable  = (desc.depth_test  && has_depth) ? VK_TRUE : VK_FALSE;
        depth_stencil.depthWriteEnable = (desc.depth_write && has_depth) ? VK_TRUE : VK_FALSE;
        depth_stencil.depthCompareOp   = to_vk_compare_op(desc.depth_compare);
        rendering_info.depthAttachmentFormat = has_depth ? to_vk_format(desc.depth_format) : VK_FORMAT_UNDEFINED;

		VkGraphicsPipelineCreateInfo pipeline_info{};
		pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipeline_info.pNext = &rendering_info;
		pipeline_info.stageCount = 2;
		pipeline_info.pStages = shader_stages;
		pipeline_info.pVertexInputState = &vertex_input;
		pipeline_info.pInputAssemblyState = &input_assembly;
		pipeline_info.pViewportState = &viewport_state;
		pipeline_info.pRasterizationState = &rasterization;
		pipeline_info.pMultisampleState = &multisample;
		pipeline_info.pColorBlendState = &color_blend;
		pipeline_info.pDynamicState = &dynamic_state;
		pipeline_info.layout = pipeline.layout;
		pipeline_info.pDepthStencilState = &depth_stencil;

		VK_ASSERT(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline.pipeline));
		VK_NAME(device, VK_OBJECT_TYPE_PIPELINE, pipeline.pipeline, desc.vertex_shader);

		vkDestroyShaderModule(device, vert_shader, nullptr);
		vkDestroyShaderModule(device, frag_shader, nullptr);

		pipeline.bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;
	}

	void vulkan_pipeline_destroy(PipelineContext& pipeline, VkDevice device, FrameContext& frame) {
		vulkan_resources_push_deletor(frame.deletion_queues[frame.current_frame], [device, pipeline]() {
			vkDestroyPipeline(device, pipeline.pipeline, nullptr);
			vkDestroyPipelineLayout(device, pipeline.layout, nullptr);
		});
	}
}
