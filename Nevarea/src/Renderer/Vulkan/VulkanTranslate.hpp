#pragma once

#include "Core/n_pch.hpp"
#include "Renderer/Vulkan/VulkanResourceManager.hpp"
#include "lib/Rendering.hpp"

namespace Nevarea::Renderer {
	VkSamplerAddressMode to_vk_address_mode(AddressMode mode);
	VkFilter to_vk_filter(Filter filter);
	VkSamplerMipmapMode to_vk_mipmap_mode(MipmapMode mode);
	VkCompareOp to_vk_compare_op(CompareOp op);
	VkBorderColor to_vk_border_color(BorderColor color);
	VkAttachmentLoadOp to_vk_load_op(LoadOp op);
	VkAttachmentStoreOp to_vk_store_op(StoreOp op);
	VkPrimitiveTopology to_vk_topology(PrimitiveTopology topology);
	VkPolygonMode to_vk_polygon_mode(PolygonMode mode);
	VkCullModeFlags to_vk_cull_mode(CullMode cull);
	VkFrontFace to_vk_front_face(FrontFace face);
	VkPresentModeKHR to_vk_present_mode(PresentMode mode);
	VkImageType to_vk_image_type(ImageType image_type);
	VkImageViewType to_vk_image_view_type(ImageType image_type);
	VkImageUsageFlags to_vk_image_usage(uint32_t usage);
	VkBufferUsageFlags to_vk_buffer_usage(uint32_t usage);
	VmaMemoryUsage to_vma_memory(MemoryLocation location);
	VkImageCreateFlags to_vk_image_create_flags(uint32_t flags, ImageType type);
	VkAccelerationStructureTypeKHR to_vk_accel_type(AccelType type);
	VkBuildAccelerationStructureModeKHR to_vk_build_mode(AccelBuildMode mode);
	VkGeometryTypeKHR to_vk_geometry_type(AccelGeometryType type);
	VkIndexType to_vk_index_type(IndexType type);
	VkBuildAccelerationStructureFlagsKHR to_vk_accel_build_flags(uint32_t flags);
	VkGeometryFlagsKHR to_vk_geometry_flags(uint32_t flags);
	VkColorSpaceKHR to_vk_color_space(ColorSpace color_space);
}
